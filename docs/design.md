# Base de design da WHYNOT LAB

Este documento define como a interface se parece e se comporta. Toda tela nova parte daqui.
O código que implementa estas regras está em `src/ui/Theme.*` (tokens, funções de desenho e LookAndFeel),
`src/ui/Icons.*` (ícones) e `src/ui/Controls.*` (botões e seletores). O autoteste
`src/app/selftest/Design.cpp` confere as regras principais e grava a galeria `design-galeria.png`.

Quando este texto e o código discordarem, um dos dois está errado: corrija um deles no mesmo commit.

## 1. Princípios

1. **Clareza vem do ícone mais o nome.** Cada ação tem um ícone que se reconhece de relance, como no FL.
   - Onde o ícone pode ser ambíguo (ferramentas, telas), o nome aparece junto.
   - Na barra de cima e na linha M S ● do mixer, o nome vem numa **legenda curta embaixo**, no estilo das legendas do
     visor (`captionFont`, `textFaint`). A revisão de iniciante de 22/10 pediu: quem nunca usou uma DAW não sabe o que
     é um círculo, um ⟲ ou "PAT".
   - Desfazer, refazer e salvar são universais: só o ícone, e a dica mostra o nome e o atalho.
   - Os nomes das notas vêm em português ("Dó3", "Fá#2"); o nome em letra (C3) aparece entre parênteses na dica.
2. **Limpo como um app da Apple.** Sem contornos em cada botão. As camadas se separam pela cor:
   fundo preto, cartões um degrau acima, controles mais um degrau. Cantos arredondados, espaço sobrando.
3. **Laranja é sinal.** `#ff751f` marca o que está ligado, aceso ou soando (nos outros temas, a cor de sinal do tema:
   veja "Temas de cores"). Isso inclui:
   - play, loop, metrônomo, passo ligado, PAT/SONG ativo, slide, mono, solo;
   - o LED do canal no rack (aceso = o canal soa), a prévia que está tocando no navegador de sons;
   - a luz do teclado-piano, porque ela muda o que as letras fazem.
   - o que está marcado: o visto dos menus e a estrela cheia dos favoritos.

   Também é laranja, porque é sinal de algo acontecendo agora:
   - uma ação em andamento (barra do scan de plugins) e o alvo de soltar (indicador na playlist, contorno do rack);
   - o foco de edição de texto (contorno e seleção);
   - o trecho do medidor perto de 0 dB (`meterMid`).

   **Mudo nunca é laranja.** No rack, o hexágono apaga quando o canal está mudo. No mixer, o M ligado usa a
   pílula neutra de "escolhido". Congelado também não: o floco usa a mesma pílula neutra. Todo o resto fica em
   preto, cinza e branco.

   **Vermelho é funcional** (`meterHigh`, `#ff3b30`): só para clipe, erro e **gravando agora**. O REC e o armar ficam
   laranja enquanto só estão prontos ("o próximo play grava") e piscam em vermelho durante a gravação. A faixa do punch
   na régua também é vermelha, porque marca onde a gravação vale.
4. **Hierarquia por tamanho, peso e tom.** O valor é branco e mais forte; o rótulo é cinza e menor;
   a legenda é cinza-escuro, em maiúsculas pequenas. Não se usa cor para dar importância.
5. **A marca manda.** Paleta e fonte vêm do GUIDE WHYNOT (preto, cinza `#a6a6a6`, branco, laranja; Montserrat).
6. **À vista só o que se usa toda hora.** Quem abre o app pela primeira vez não pode tropeçar em ícone sem nome.
   - O que se usa sempre fica à vista, com nome quando o ícone sozinho não basta.
   - O que é raro vai para o botão direito, como congelar e renomear.
   - O que só serve a quem procura aparece com o mouse em cima, como o armar das notas no rack e a estrela dos
     favoritos.
   - O padrão fica em cinza e o que foi mudado fica em branco. Exemplo: a saída de um canal que vai para a própria
     trilha é cinza, e um canal mandado para um grupo fica em branco.
   - Toda a interface é em português, inclusive os nomes padrão ("Raia 1", não "Track 1").

## 2. Estrutura da janela

```
┌──────┬────────────────────────────────────────────────────────────────────────────┐
│  W   │ Arquivo ▾  ↶ ↷ 💾      PAT|SONG ▶ ■ [ 1.1.1 │ 140,00 │ 4/4 ] ⟲ 𝅘     ▦ Beat ▾ +   🔈 ▬▬ │
├──────┼──────────────┬─────────────────────────────────────────────────────────────┤
│ ▤    │ ╭──────────╮ │ ╭─────────────────────────────────────────────────────────╮ │
│Playl.│ │ Sons     │ │ │ cartão da tela de cima: Playlist, Channel rack ou       │ │
│ ▦    │ │   ou     │ │ │ Piano roll (faixa de 48 px no topo do cartão)           │ │
│ Rack │ │ Plugins  │ │ ╰─────────────────────────────────────────────────────────╯ │
│ 🎹   │ │ (272 px, │ │ ╭─────────────────────────────────────────────────────────╮ │
│Piano │ │ opcional)│ │ │ cartão do mixer                                         │ │
│ ──── │ ╰──────────╯ │ ╰─────────────────────────────────────────────────────────╯ │
│ ⫼    │              │                                                             │
│Mixer │                                                                            │
│ 📁   │                                                                            │
│ Sons │                                                                            │
│ 🔌   │                                                                            │
│Plugin│                                                                            │
│ ⚙    │                                                                            │
│Áudio │                                                                            │
├──────┴────────────────────────────────────────────────────────────────────────────┤
│ status                                                         Interface 100%     │
└───────────────────────────────────────────────────────────────────────────────────┘
```

- **Trilho (76 px, à esquerda):** a marca no topo, alinhada com a barra de cima. Depois, um botão por tela
  (ícone de 22 px e nome de 11 px embaixo): Playlist, Rack, Piano e Projetos; um fio; e as camadas que ligam e desligam
  à parte: Mixer, Sons e Plugins. O áudio fica no pé. O item aceso tem fundo `raised` (o trilho fica sobre o preto)
  e texto branco semibold. Atalhos: F5, F6, F7, F11 (Projetos), F9, Alt+F8 (Sons), F8 (Plugins) e F10.
- **Navegadores (cartão de 272 px entre o trilho e as telas):** Sons ou Plugins, um de cada vez, no mesmo lugar.
  - Igual nos dois: título na faixa de 48 px com as ações à direita; busca de 32 px com a lupa de 16; lista com
    linhas de 28 (Sons) ou 30 px (Plugins).
  - O rodapé muda. Sons: a contagem em `textDim` e uma dica de uso em `textFaint`. Plugins: o andamento do scan
    (ou a contagem) em `textDim` e os botões Pastas e Bloqueados.
- **Barra de cima (56 px):** três grupos.
  - Esquerda: Arquivo, desfazer, refazer e salvar.
  - Centro: PAT | SONG, tocar, parar, gravar (REC), o **visor** e, ao lado dele, loop, metrônomo e o teclado-piano
    (ícone `keyboard`: as letras do computador tocam o canal escolhido, Ctrl+T).
  - Os botões ficam alinhados com o topo do visor (y 7). Embaixo dos do centro, a legenda (y 40 a 50, a base do visor):
    PADRÃO e MÚSICA sob o PAT | SONG, depois TOCAR, PARAR, GRAVAR, LOOP, CLIQUE e TECLADO. Cada botão ocupa a largura
    da própria legenda, para as palavras não se encostarem.
  - Direita: padrão atual, novo padrão, o medidor de CPU e a saída master.

  O centro fica no meio da **barra** (que começa depois do trilho, então 38 px à direita do meio da janela),
  quando cabe; senão, no meio do espaço que sobra. Em tela estreita, o medidor master sai primeiro, depois a CPU,
  e por fim o visor encolhe.
- **Visor:** um cartão rebaixado com três leituras (POSIÇÃO, BPM, COMPASSO), valores de 19 px (o compasso em 17)
  e a legenda embaixo. Os números usam largura fixa por dígito, então não "dançam" enquanto toca.
- **Cartões:** as telas ficam sobre o preto, com cantos de 12 px. Há fresta de 8 px entre os cartões e na borda
  direita; à esquerda o cartão encosta no trilho (ou no navegador), e em cima, na barra, que já são pretos.
  A fresta entre a tela de cima e o mixer é a alça de redimensionar; uma pílula aparece quando o mouse chega.
- **Tela de abertura** (`SplashWindow`): enquanto o motor, os plugins e o piano carregam, um cartão de 560 × 340 px no
  centro da tela mostra a logo branca, "WHYNOT LAB" em semibold com espaçamento largo, um risco laranja de 32 px,
  "Abrindo o estúdio..." em `textFaint` e a versão no canto. Fica pelo menos 0,9 s e some num fade de 0,25 s quando a
  janela abre. É sempre nas cores da marca, qualquer que seja o tema. Não aparece no autoteste, nas capturas nem no
  `--render`.
- **Barra de status (28 px, `StatusBar`):** a mensagem à esquerda em 13 px; à direita, a luz do teclado-piano (quando
  ligado) e a escala da interface em `textFaint`.
  - O aviso comum (`showInfo`) some em 7 s.
  - O **erro** (`showError`) fica até ser fechado (×) ou trocado. Ele aparece numa pílula no vermelho funcional suave,
    com o ícone de aviso, o texto em branco e, quando há, o botão da saída ("Abrir Áudio e MIDI").
  - A **orientação** (`showNote`) também fica até fechar, numa pílula neutra (`raised`) com um "i". Por exemplo, depois
    do "Comece aqui": "aperte o play (espaço)".
  - Sem mensagem, mostra o problema que continua valendo (um canal sem sample), em `warning`.
  - Play sem placa de som aberta: o erro "Sem saída de som", com o botão "Abrir Áudio e MIDI" (onde fica o teste).
- **Comece aqui** (`WelcomeCard`): num projeto novo, um cartão flutuante (`floating`, cantos de 12) no canto de cima à
  direita da tela de cima, com três passos numerados. O número de cada passo vai no círculo do sinal.
  - Passo 1: pôr um beat de exemplo. Kick, clap e hats no padrão, num gesto só de desfazer.
  - Passo 2: gravar a voz. Mostra o mixer e a orientação.
  - Passo 3: exportar em MP3.
  - O × fecha; "Não mostrar de novo" vale para as próximas aberturas (chave `welcomeHidden` no `WHYNOT LAB.settings`).
  - Depois de um passo, o cartão encolhe para a pílula "Comece aqui", que abre de novo com um clique. Ele sai quando um
    projeto do disco é aberto.

## 3. Tokens

### Cores (`theme::colour`)

| Token | Valor | Uso |
|---|---|---|
| `background` | `#000000` | chão da janela: trilho, barra de cima, frestas |
| `panel` | `#141414` | cartões (telas), visor, cartão com seta |
| `raised` | `#1f1f1f` | controle em repouso (botão, lista, passo desligado); item aceso do trilho |
| `raisedHigh` | `#2c2c2c` | mouse em cima; um pouco mais clara, a pílula de "escolhido" |
| `line` | `#262626` | fios e divisórias |
| `text` | `#ffffff` | valores, títulos, o que importa |
| `textDim` | `#a6a6a6` | rótulos, ícones em repouso |
| `textFaint` | `#8a8a8a` | dicas, legendas, desabilitado (4,5:1 sobre o cartão: a dica tem que ser lida) |
| `signal` | `#ff751f` | ligado, aceso, soando (ver o princípio 3) |
| `signalTint` | laranja a 18% | fundo do que está ligado |
| `meterLow` | `#d9d9d9` | medidor, trecho baixo |
| `meterMid` | `#ff751f` | medidor, trecho perto de 0 dB |
| `meterHigh` | `#ff3b30` | clipe e erro (vermelho funcional) |
| `warning` | `#ff9a57` | avisos na barra de status |

Menus, dicas e a borda do cartão com seta usam duas cores internas do Theme: fundo `#1a1a1a` e borda `#333333`.

**Grade da linha do tempo** (`theme::colour::grid`, na playlist): degraus abaixo e pouco acima do `panel`, para a
grade se ler sem competir com os blocos.

| Token | Valor | Uso |
|---|---|---|
| `grid::laneEven` / `laneOdd` | `#0c0c0c` / `#101010` | raias alternadas |
| `grid::fine` | `#131313` | divisões do passo (1/2 a 1/16), quando o zoom mostra |
| `grid::step` | `#151515` | passo |
| `grid::beat` | `#181818` | tempo |
| `grid::laneEdge` | `#1c1c1c` | fio entre raias |
| `grid::clipHead` | `#222222` | faixa do nome do clipe de áudio (o corpo do clipe é `panel`) |
| `grid::rowOut` | `#0a0a0a` | piano roll: linha fora da escala |
| `grid::rowDark` | `#111111` | piano roll: linha de tecla preta (escala cromática) |
| `grid::row` | `#181818` | piano roll: linha na escala (ou tecla branca, na cromática) |
| `grid::rowRoot` | `#222222` | piano roll: a tônica |

O compasso usa `line`, e a cada 4 compassos (a frase do trap) `textDim` a 30%.

**Teclado do piano roll** (`theme::colour::keys`): claro, como um teclado de verdade. Branca `#d9d9d9`,
preta `#1b1b1b`, fio entre teclas `#8c8c8c`, mouse em cima `#bfbfbf` (branca) e `#3a3a3a` (preta), nome do Dó
`#2e2e2e`. A tecla que soa acende em laranja.
Cada canal tem a cor dele, que aparece só como uma pílula fina na placa do nome.

### Temas de cores (`theme::ColourScheme`)

O público escolhe o tema em **Arquivo → Tema de cores**. A troca é na hora, em todas as janelas abertas, e a escolha fica
salva no `WHYNOT LAB.settings` (chave `theme`). O **WHYNOT** é o padrão; os outros mudam só as cores. Medidas, fontes,
ícones e o lugar de cada coisa ficam iguais.

| Tema | Chão | Cartão | Controle | Texto | Rótulo | Sinal |
|---|---|---|---|---|---|---|
| WHYNOT (padrão) | `#000000` | `#141414` | `#1f1f1f` | `#ffffff` | `#a6a6a6` | laranja `#ff751f` |
| Claro | `#e8e8eb` | `#fbfbfc` | `#eeeef1` | `#141416` | `#4f4f56` | laranja `#f06414` |
| Colorido | `#0b0816` | `#151027` | `#201936` | `#ffffff` | `#b8acd9` | rosa `#ff4fb8` (medidor ciano) |
| Frio | `#05080c` | `#0e141b` | `#172029` | `#f1f6fa` | `#9db0c2` | azul-gelo `#39c2ff` |

- **O sinal muda com o tema; o vermelho funcional, não.** O princípio 3 vale para a cor de sinal de cada tema: o laranja
  no WHYNOT e no Claro, o rosa no Colorido, o azul-gelo no Frio. O `meterHigh` (`#ff3b30`) é o mesmo nos quatro, porque
  clipe, erro e "gravando agora" não podem mudar de cor.
- **Contraste mínimo sobre o cartão** (WCAG, conferido no autoteste): texto 7:1, rótulo e dica 4,5:1 (o rótulo sempre
  mais forte que a dica), sinal 3:1, e a nota sobre o sinal 4,5:1.
- **Quem desenha lê o token na hora de pintar.** Nunca guarde uma cor de token numa variável estática ou num membro.
  O que foi guardado com `setColour (id, token)` é refeito na troca pelo `theme::reskinAll`, que acha o token pela cor
  e mantém o alfa.
- A paleta inteira de cada tema (grade, piano roll, teclado, menus) fica em `Theme.cpp`, nas funções `make*`.
- Dois tokens existem para os temas não se confundirem:
  - `shade` é o tom de escurecer: fios por cima das linhas, a área depois do fim do padrão.
  - `onSignal` é a tinta sobre o sinal: o nome da nota, as marcas do roll nos passos.
  - Não use `background` para isso. No Claro o chão é claro, e o fio some ou o nome da nota fica ilegível.
  - Nos temas escuros os dois são o preto (ou quase), então o WHYNOT fica igual ao de antes.
- A logo sai na cor do texto do tema (`theme::drawLogo`): branca no escuro, quase preta no Claro.
- Na linha de comando, `--tema=<whynot|claro|colorido|frio>` escolhe o tema de uma execução. Sem ele, as capturas
  (`--screenshot`) usam o padrão.

### Medidas (`theme::metric`)

Espaços na escala **4, 8, 12, 16, 24**. Uma exceção: **2 px entre botões irmãos de um mesmo grupo**
(desfazer | refazer | salvar, loop | metrônomo, as ações do cabeçalho de um navegador).

| Token | Valor | Uso |
|---|---|---|
| `gap` | 8 | fresta entre cartões; distância entre grupos de controles |
| `pad` | 16 | margem interna dos cartões |
| `control` | 32 | altura de botões, listas e seletores |
| `toolbar` | 48 | faixa do topo de todo cartão (rack, piano roll, playlist, mixer, navegadores) |
| `topBar` | 56 | barra do transporte |
| `rail` | 76 | trilho das telas |
| `status` | 28 | barra de status |
| `radiusSmall` | 5 | passos, notas |
| `radius` | 8 | botões, listas, campos, dicas |
| `radiusCard` | 12 | cartões e cartão com seta (os menus usam 10) |

### Texto (Montserrat, `theme::font (altura, Weight)`)

Pesos: `Weight::regular`, `medium`, `semibold` e `bold`. Tamanhos com nome em `theme::type`: `title` 15,
`body` 13 e `small` 12.

| Nível | Tamanho e peso | Uso |
|---|---|---|
| título | `type::title` (15), semibold | nome do cartão ("Channel rack", "Mixer", "Sons") |
| corpo | `type::body` (13), medium | botões, listas, nomes no mixer (no rack o nome é 14, nas linhas de 40 px) |
| detalhe | `type::small` (12) | saídas no mixer, rótulos curtos ("Clique coloca"), a escala no pé |
| dica | `type::body` (13), `textFaint` | as dicas na tela (o rodapé do rack, a automação, o navegador de sons): nunca abaixo de 13 px |
| números | `numberFont (h)`, semibold | BPM e posição (19), compasso (17), dB (13) |
| legenda | `captionFont()`: 9,5, semibold, MAIÚSCULAS, espaçada, `textFaint` | rótulos do visor, títulos de grupo |
| marca | `displayFont (h)`, bold | reservado para a Horizon, quando o arquivo chegar |

Outros tamanhos fixos:
- 11 nos nomes do trilho e na **régua da linha do tempo** (piano roll e playlist): números e, de perto, tempo e passo
  ("3.2", "3.2.4"), em `textFaint`, com marquinhas nas divisões;
- 12,5 nas dicas (tooltip).

### Funções de desenho (`Theme.h`)

- `drawNamePlate` (com um ícone de estado opcional no canto direito, como o floco de congelado), `drawRoutePill`
  e `hexagon()`, o LED de mute do rack.
- `drawTabularText` (dígitos de largura fixa) e `cornerMask` (recorta os cantos dos cartões).
- `styleComboBox (box, Icon, bare)`.
- `fromHex`, `logo()` e `iconImage (size)`.

## 4. Ícones

Desenhados por nós em vetor (`src/ui/Icons.cpp`), numa grade de 24 x 24, com traço de 2 unidades e pontas
redondas. Formas cheias só onde ajudam a ler: transporte, notas, passos ligados, blocos. Nada é copiado de outro app.

- **Como usar:** `icons::draw (g, Icon::..., área, cor)`. O ícone ocupa o quadrado centrado na área, e o traço
  acompanha o tamanho.
- **Exceções do traço:** `channelRack` usa 1,4, porque os passos são pequenos; `select` é tracejado com 1,8.
- **Para acrescentar um ícone:** ponha no grupo certo do `enum class Icon`. **`warning` fica sempre por último**:
  ele marca o tamanho da tabela. Depois, acrescente a linha na tabela abaixo. A galeria do autoteste confere que
  todos desenham alguma coisa.

| Ícone | Onde | Significado |
|---|---|---|
| `play` / `pause` / `stop` | transporte | tocar, pausar (aparece enquanto toca), parar |
| `loop`, `metronome` | transporte; `loop` também nos diálogos de exportação | repetir, metrônomo |
| `undo`, `redo`, `save` | barra de cima; `save` também na exportação | desfazer, refazer, salvar |
| `playlist`, `channelRack`, `pianoRoll`, `mixer` | trilho. Também: `playlist` na exportação, `channelRack` no seletor de passos, `pianoRoll` na lista de canal, `mixer` no botão "Estilo de mixagem" | as quatro telas |
| `settings` | trilho | áudio e MIDI |
| `projects` | trilho | tela Projetos: um disco (a borda, o selo e um sulco) |
| `folder`, `plugin` | trilho (Sons, Plugins) e navegadores | pasta, plugin |
| `search` | busca dos navegadores, botão Escanear, diálogo de áudio | busca |
| `wave` | navegador de sons | arquivo de áudio |
| `keyboard` | barra de cima | teclado-piano (o teclado do computador tocando) |
| `pencil`, `brush`, `select` | piano roll e playlist | desenhar, pintar, selecionar |
| `scissors`, `eraser`, `slip` | ferramentas da playlist (`scissors` também no piano roll) | cortar, apagar, deslizar o conteúdo do bloco |
| `magnet` | listas de grade | encaixe (snap) |
| `slide`, `timer` | piano roll | glide do 808 e tempo do glide |
| `note` | piano roll (tom); navegador de plugins (instrumento) | nota, instrumento |
| `pattern` | barra de cima, playlist, exportação | padrão |
| `star` / `starFilled`, `dice` | presets; navegadores, menus de efeito e o "+" do rack (favoritos) | favorito (contorno = não, cheia = sim), variar |
| `chevronLeft` / `chevronRight` | presets; `chevronRight` também nos submenus | anterior / próximo |
| `route` | canais e trilhas | para onde vai o som |
| `speaker` | barra de cima | saída master |
| `record` | barra de cima (REC) e mixer (armar) | gravar, armar para gravar |
| `snowflake` | mixer e placa do nome (rack e mixer) | congelar, congelado |
| `automation` | painel de automação da playlist | curva de automação |
| `headphones` | placa do nome no mixer, quando a monitoração não é a padrão (gravação de voz) | monitorar a entrada |
| `plus`, `chevronDown`, `check`, `warning` | geral (`check` também na exportação, `warning` em Bloqueados e no medidor de CPU) | adicionar, abre menu, marcado, aviso |

Tamanhos:
- 16 px em botões e segmentos de 32 px (o IconButton usa metade da altura, de 14 a 20 px), nas listas e na busca;
- 14 px nas pílulas de saída e nas setinhas de menu;
- 22 px no trilho.

## 5. Componentes

- **IconButton** (`Controls.h`): `IconButton (nome, Icon, rótulo = {}, Style = ghost)`.
  - O `nome` vira a dica.
  - Métodos: `setIcon`, `setLabel`, `setStyle`, `setShowsMenuArrow` (a setinha de "abre menu") e
    `getIdealWidth (altura)`.
  - `ghost`: sem fundo até o mouse passar, tinta `textDim`. É o padrão das barras.
  - `filled`: fundo `raised` e tinta branca. É para as ações principais (tocar, parar, "Tocar a playlist").
  - Ligado: fundo `signalTint` e ícone e rótulo laranja.
  - `setNeutralWhenOn (true)`: para botão que só abre ou fecha um painel (Ideias, por exemplo). Ligado, ele ganha a
    pílula de "escolhido" (`raisedHigh` um pouco mais clara, tinta branca). O rótulo fica medium.
  - `setAlert (0 a 1)`: **gravando agora**. Fundo e tinta em `meterHigh`, com a intensidade para piscar (1 e 0,35, a
    cada meio segundo). 0 devolve o botão ao estado normal. Só o REC e o armar usam.
- **TextButton** (pelo LookAndFeel, usado no M/S do mixer): fundo `raised`, sem contorno, texto semibold de 12 a 13.
  Ligado em `signalTint` com texto laranja. Com a propriedade `neutral`, ligado usa a pílula de "escolhido" (é o M:
  mudo não é laranja).
- **SegmentedControl**: `SegmentedControl (accent = false)`, com `setItems ({ {rótulo, Icon, dica}, ... })`,
  `setSelected`/`getSelected`, `onChange (índice)`, `getIdealWidth()` e `setLabels (Labels::all | none | selected)`
  (`setShowsLabels (bool)` é o atalho para `all` e `none`).
  - Segmentos de largura igual, e a dica é de cada segmento.
  - A escolha ganha a pílula de "escolhido" com texto semibold. Com `accent`, a pílula é laranja
    (PAT | SONG: o que vai tocar).
  - `none` deixa só os ícones, para tela estreita.
  - `selected`: só a escolha mostra o nome e se alarga para ele; as outras ficam no ícone, e a largura total não muda
    quando a escolha muda. Use quando há muitas opções e o nome de todas não cabe. Quem olha sempre sabe que
    ferramenta está na mão.
  - Ferramentas da playlist (8): sempre `selected`. Piano roll (4): `all` a partir de 1320 px, `selected` de 1240
    a 1320 px e `none` abaixo disso, para o tom não ser cortado.
- **ChoiceChips** (`Controls.h`): opções que se excluem em pílulas de 28 px que quebram linha (gênero, clima, parte e
  compassos no Ideias). `setItems`, `setSelected`/`getSelected`, `onChange` e `getHeightForWidth (largura)`, para o
  painel saber a altura. A escolhida ganha a pílula `raisedHigh` com texto semibold; as outras ficam em `raised`.
  Para poucas opções numa linha só, use o SegmentedControl.
- **AmountSlider** (`Controls.h`): quantidade de 0 a 100%. O nome à esquerda (100 px), trilho fino com o trecho aceso
  e a bolinha branca, e o valor à direita em `numberFont`. Duplo clique volta a 50%. `setValue`/`getValue`; quem usa lê o valor na hora de agir.
- **Listas** (`theme::styleComboBox (box, Icon, bare)`): fundo de controle, ícone opcional à esquerda e setinha à direita.
  - `bare` tira o fundo até o mouse passar.
  - O visor usa o estilo `lcd`, uma propriedade `style` posta à mão: sem fundo, centrado, sem setinha.
  - O texto fica sempre numa linha: encolhe até 70% e depois ganha reticências. Lista estreita demais pede mais
    largura, não duas linhas.
- **Placa de nome** (`drawNamePlate`): fundo de controle, pílula fina com a cor do canal e nome em branco.
  Borda vermelha quando falta o sample. O ícone de estado opcional (13 px, `textDim`) fica no canto direito e o nome
  encolhe para dar lugar: hoje é o floco de congelado.
- **Pílula de saída** (`drawRoutePill`): ícone de rota e o destino. O destino padrão fica em cinza, no rack e no mixer:
  o Master, a placa de som (na master) e, no rack, a trilha que o mixer criou para o próprio canal ("Kick" → "Kick").
  Um destino escolhido, como um grupo, fica em branco.
- **Knob** (LookAndFeel, `drawRotarySlider`): arco de fundo, arco do valor em cinza (o pan parte do centro),
  miolo e ponteiro branco.
  - Com a propriedade `accent`, o arco fica laranja: é o parâmetro agindo agora, como a redução de ganho de um
    compressor.
  - Valor em `numberFont` embaixo, nome em `captionFont`.
- **Fader**: trilho fino, trecho abaixo da capa aceso, marcas de 0 dB e capa com risco branco.
- **Menus**: cartão flutuante `#1a1a1a` com borda `#333`, cantos de 10, borda interna de 5 e itens de 30 px.
  O item marcado ganha um visto laranja na coluna da esquerda, que existe sempre para alinhar os itens.
- **Dicas**: o mesmo cartão flutuante, com cantos de 8 e texto de 12,5. O atalho entre parênteses no fim sai em
  cinza, então escreva "Ação (atalho)".
- **Barras de rolagem**: 10 px de área, com a barra de 4 px que vai a 6 px com o mouse.
- **Cartão com seta** (`juce::CallOutBox`, pelo LookAndFeel): ajustes rápidos que abrem apontando para o controle que
  os chamou.
  - Fundo `panel`, borda fina `#333`, cantos de 12, sombra larga e suave e 20 px de borda para a seta.
  - O conteúdo pinta `panel` e se funde com a moldura; a margem interna é `pad`.
  - Hoje só a galeria do autoteste usa. O primeiro uso real é o painel do sampler, que entra com o merge do Sampler.
- **Duplo clique no nome de um canal**: hoje, no VST3, abre a janela do plugin; nos outros canais, renomeia. Depois
  dos merges do Sampler e do Synth no `integracao-fds`, o duplo clique abre o instrumento do canal: o painel do
  sampler, a janela do WHYNOT Synth ou a do VST3. Renomear passa para o menu do botão direito, e a dica da placa
  acompanha.
- **Navegador de sons** (`SampleBrowser`): lista em árvore com o Kit WHYNOT, a **Biblioteca**
  (`Músicas/WHYNOT LAB/Biblioteca`, onde o usuário põe drum kits, loops e one-shots) e as pastas extras.
  - Pasta = seta + ícone de pasta; som = ícone de onda + nome, com a extensão em cinza à direita.
  - Clique (ou setas) ouve a prévia, e o som que está tocando acende em laranja.
  - Arrastar leva para o rack (canal novo; solto no nome, troca o sample) ou para a playlist (clipe de áudio).
    Duplo clique ou Enter cria um canal.
  - Na busca, a pasta de cada som aparece à direita.
  - A Biblioteca vazia mostra a dica de onde pôr os kits; as outras pastas vazias dizem "Nenhum som nesta pasta".
- **Favoritos** (`FavoriteStar.h`, desde 21/10): a estrela fica no fim da linha, numa coluna de 26 px.
  - Favorito: `starFilled` em laranja, como o visto dos menus. Não favorito: o contorno `star` em `textDim` só aparece
    com o mouse na linha ou na linha escolhida; com o mouse em cima da estrela, fica branco. A lista fica limpa.
  - Clicar na estrela marca e desmarca sem tocar a prévia, sem abrir a pasta e sem carregar o preset. O botão direito
    também tem "Marcar como favorito".
  - Nos navegadores, a estrela do cabeçalho (IconButton, ligado = laranja com `starFilled`) mostra só os favoritos.
    Na lista inteira nada muda de lugar ao marcar; no filtro, o desmarcado sai na hora.
  - Nos menus de adicionar (slot do mixer, presets de efeito, "+" do rack, presets do Synth), os favoritos vêm primeiro,
    numa seção "Favoritos", e cada item tem a estrela clicável (`favui::menuItem`): clicar nela não fecha o menu.

### Tela Projetos (21/10)

- Cartão da tela de cima (F11), no lugar da playlist, do rack ou do piano roll.
- **Faixa de 48 px:** o título e a contagem em `textFaint` (acima da barra lateral); a busca de 32 px (fundo `raised`, a lupa
  de 16, alinhada com o começo da lista); e, à direita, Atualizar (`search`), Pendentes e Renderizar (`wave`) e Abrir
  (`folder`), todos `ghost` com rótulo.
- **Barra lateral (184 px):** linhas de 28 px com ícone de 14 e a contagem à direita em `numberFont` 12 `textFaint`; a
  escolhida ganha a pílula `raisedHigh` com texto semibold. Legendas STATUS, TAGS e PASTAS em `captionFont`. A estrela de
  Favoritos fica laranja quando há algum. Um fio `line` separa a barra da lista.
  - COMBOS: cada combo com o ícone `pattern` (vira `play` laranja enquanto o combo toca) e "Novo combo" (`plus`).
  - Arrastar uma linha da lista sobre um combo: o combo-alvo acende em `signalTint` com o contorno laranja (o alvo de soltar).
- **Vista do combo:** a posição ("1.", em `numberFont` 12 `textFaint`) antes do nome; "a mais nova" e a nota ("— nota")
  depois do nome, em 12 `textFaint`; uma referência externa mostra "Referência" na coluna Status. O rodapé diz
  "Combo <nome>: 2 de 5" enquanto toca.
- **Janela Importar (520 px, cartão `panel`):** o título "Importar de X" em 15 semibold e a linha "BPM · tom · N padrões" em
  12 `textDim`; a legenda PADRÕES e uma caixa de marcar por padrão (30 px, com os canais e as contagens), rolando depois de
  6; a legenda OPÇÕES com as duas caixas; o erro, se houver, em `meterHigh`; Cancelar e Importar (`filled`, `plus`) à direita.
- **Legendas das colunas** (24 px, `captionFont`): NOME, BPM, TOM, DURAÇÃO, LUFS, ONDA, SALVO e PRÉVIA. A ONDA e o LUFS saem
  primeiro quando a tela estreita.
- **Linhas de 40 px:** o ▶ (16 px) à esquerda; a estrela (15 px: cheia e laranja no favorito; o contorno `textDim` só na
  linha escolhida); nome em 13 medium, seguido das versões e das tags em 12 `textFaint`; números em `numberFont` 13 com
  largura fixa por dígito; a onda da prévia em `textFaint`; o status em 12 `textDim`. A escolhida ganha a pílula `raisedHigh`.
  - A cor do projeto é um fio de 3 px na borda esquerda da linha, como a pílula de cor das placas (a paleta dos canais).
  - Pilha de versões: a seta (`chevronRight`/`chevronDown`, 12 px) antes do nome; as versões mais velhas, abertas, ficam
    recuadas 20 px e com o nome em `textDim`.
- **Estado da prévia:** Pronta (`check`) e Desatualizada (`warning`) em `textDim`; Sem prévia em `textFaint`; renderizando é
  uma barra fina laranja com a porcentagem (ação em andamento); Falhou em `meterHigh`.
- **O que toca acende em laranja:** o ▶ vira pausa, o nome e o trecho tocado da onda da linha, e a agulha do deck.
  A linha que está num deck mostra a letra dele (A ou B) numa pílula de 18 px no fim do nome, laranja se é o que soa.
- **Decks A e B (164 px, no pé):** fio `line` em cima.
  - Faixa de controles de 32 px: tocar/pausar e parar `filled` de 40 px; o A | B (`SegmentedControl` com `accent`: laranja
    no que soa); a sincronia (lista com `magnet`); "Igualar volume" (`speaker`, ligado = laranja); o loop (lista com `loop`);
    e, à direita, Referência (`folder`).
  - Uma raia de 48 px por deck: a letra numa pílula de 24 px (laranja no que soa, `raisedHigh` no que vai soar, apagada no
    outro); o nome em 13 semibold e "BPM · LUFS · ajuste em dB" em 12 `textFaint`; a onda inteira num fundo `raised`, com
    o trecho do loop em `signalTint` e o que já tocou mais claro; a agulha laranja só no deck que soa; o tempo à direita.
  - Deck vazio: uma dica em `textFaint` dizendo como carregar.
- Rodapé da lista (28 px): o que a fila de prévias faz agora, ou a dica de uso em `textFaint`.

### Gravação, automação e desempenho (3.5)

- **REC** (barra de cima, depois do parar): IconButton `filled` com o ícone `record`. Ligado (laranja) quer dizer
  "o próximo play grava"; gravando, pisca em vermelho (`setAlert`). Atalho R.
- **Armar no mixer**: o terceiro botão da linha do M e do S (M S ●), como num console. É um IconButton `filled` com o
  ícone `record`. O master não tem. Embaixo da linha, as legendas MUDO, SOLO e REC.
  - Armada: o botão fica laranja e um medidor fino da entrada (3 px, `meterLow`; vermelho acima de -0,5 dB) aparece
    logo abaixo do resumo dos efeitos. Uma trilha armada por vez, porque a entrada da placa é uma só.
  - Gravando: o armar pisca em vermelho, como o REC.
- **Armar no rack** (notas ao vivo): uma coluna de 20 px entre o LED de mute e o nome. Desarmado, o aro (`textFaint`)
  só aparece com o mouse na linha. Armado, a bolinha cheia em laranja, sempre à vista; gravando, em vermelho. Canal de
  áudio não tem: ele grava pela trilha do mixer.
- **Congelar**: no botão direito do nome, no mixer ("Congelar trilha") e no rack ("Congelar canal", desabilitado e
  com o motivo quando o canal sai direto no master). Congelado, o floco aparece na placa do nome, no rack e no mixer.
  Os canais que saem pela mesma trilha congelam juntos, e a barra de status diz quais. A dica do medidor de CPU, quando
  ele acende, diz onde congelar.
- **Medidor de CPU** (barra de cima, à esquerda do medidor master, 144 px): legenda CPU, trilho de 40 px com a carga e
  uma marquinha no pico, o número em `numberFont` e, se houve falha de buffer, o aviso com a contagem. Fica laranja com
  a CPU em 85% ou mais, ou com falha nos últimos 3 s. Um clique zera as falhas; a dica sugere congelar ou subir o buffer.
- **Punch na régua da playlist**: a faixa vermelha de 4 px no alto da régua, com as duas pontas de 12 px, e uma
  sombra vermelha bem leve (5%) com dois fios no trecho da grade. Botão direito na régua abre o menu do punch (só este
  compasso, punch in aqui, punch out aqui, ligar ou desligar); com o punch ligado, as pontas arrastam com o encaixe
  da grade (Alt solta).
- **Automação** (painel embaixo da grade da playlist, na mesma linha do tempo):
  - Cabeçalho de 28 px: o ícone `automation` com a legenda AUTOMAÇÃO, uma dica do modo em `textFaint` e o botão
    "Nova curva". Sem curvas, o painel é só o cabeçalho.
  - Uma raia de 56 px por curva, até 3 à vista (as outras rolam com a rodinha). A coluna da esquerda tem os mesmos
    150 px das raias da playlist: o alvo ("Voz · Filtro · Frequência") e o valor de agora em `numberFont`.
  - A curva anda reta entre os pontos, com uma sombra leve embaixo. No modo SONG ela toca e fica **laranja**; no PAT
    fica parada e cinza. Os pontos são bolinhas de 7 px vazadas; com o mouse em cima ou na mão, cheias.
  - Clique cria um ponto, arrastar move (com o encaixe da grade; Alt solta), botão direito no ponto apaga. Botão
    direito no nome: apagar os pontos ou a curva. Cada arrasto é um passo de desfazer.
  - "Nova curva" abre um submenu por trilha do mixer (volume, pan e os parâmetros dos efeitos) e por instrumento
    VST3. O que já tem curva aparece marcado e só rola até ela. A curva nova nasce com um ponto no valor de agora.
  - Alvo que saiu (efeito apagado): o nome em `textFaint` e "Saiu: apague a curva".

## 6. Estados

| Estado | Como aparece |
|---|---|
| repouso | `ghost`: sem fundo, tinta `textDim`. `filled`, lista e TextButton: `raised`, tinta branca |
| mouse em cima | `raisedHigh`; ícone e texto brancos |
| pressionado | `raisedHigh` mais clara |
| escolhido (segmento, botão neutro, M do mixer) | pílula `raisedHigh` um pouco mais clara, texto branco (semibold no segmento) |
| escolhido no trilho | fundo `raised` (sobre o preto), texto branco semibold |
| ligado / soando | `signalTint` + laranja |
| desabilitado | IconButton, segmento e lista: mantêm o fundo, tinta `textFaint`. TextButton: fundo a meia opacidade |
| problema | contorno `meterHigh` + ícone de aviso na barra de status |
| gravando agora (REC, armar) | fundo e tinta `meterHigh`, piscando a cada meio segundo |
| congelado | floco na placa do nome; o botão do floco com a pílula neutra |

## 7. Ao criar uma tela nova

1. A tela é um cartão: fundo `panel`. O MainComponent recorta os cantos.
2. No topo, uma faixa de `metric::toolbar` (48 px). À esquerda, o título (15 semibold) ou o seletor principal.
   Os controles têm 32 px, centrados na faixa, e os grupos ficam separados por 16 px ou por um fio `line`.
3. Toda ação tem ícone. Ponha o rótulo junto quando o ícone sozinho puder confundir. A dica leva o nome e o atalho.
4. Laranja só como no princípio 3. Teste em escala de cinza: a hierarquia tem de continuar clara.
5. Nada de tamanho solto: use `metric`, `type` e a escala 4/8/12/16/24 (2 px só entre botões irmãos).
6. Texto com acento usa o sufixo `"..."_t`.
7. Para ler pixel de uma imagem (teste, captura), use `juce::SoftwareImageType()` e feche o `Graphics` antes de ler.
   No Windows, a imagem nativa fica no Direct2D, e a leitura com o desenho aberto trava.

## 8. Galeria (autoteste)

`runDesignSelfTest` (`src/app/selftest/Design.cpp`) monta, fora da área de trabalho, uma prancha com:
- botões: ghost, filled, ligado, neutro, desabilitado e o REC gravando (`setAlert`);
- segmentos, listas, placas, pílulas, knobs (normal e `accent`) e fader;
- as pílulas de escolha (ChoiceChips) e a quantidade (AmountSlider);
- a folha de ícones e um cartão com seta.

E confere:
- o botão ligado é laranja, e o neutro é cinza, sem laranja;
- o REC gravando é vermelho, não laranja;
- o desabilitado não acende;
- o cartão com seta tem cantos de 12 e fundo `panel`;
- todos os ícones desenham alguma coisa.

A prancha sai em `selftest_work/design-galeria.png`. Os quatro temas lado a lado saem em
`selftest_work/design-temas.png`, com a conferência do contraste e da troca ao vivo. Olhe a prancha a cada mudança no Theme, nos Icons ou nos Controls.
