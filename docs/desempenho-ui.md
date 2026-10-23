# Desempenho da interface e fluidez

Pedido do usuário (21/10, à noite): animações fluidas, sem travamentos, e o app rápido para quem faz o beat em 1 minuto.
Esta página guarda o que foi medido, o que mudou e os números de antes e depois. Frente Fluidez, branch `extra-fluidez`.

## Como medir

O autoteste tem as seções "Fluidez" (`src/app/selftest/Fluidity.cpp`). Elas montam, por comandos, um projeto grande:
- 29 canais (28 de sampler e 1 de áudio);
- 4 padrões, com 343 passos ligados e 384 notas;
- 162 blocos na playlist (150 de padrão e 12 clipes de áudio);
- 30 trilhas de mixer, 6 delas com 2 ou 3 efeitos (15 no total).

O JSON dele tem 154 KB. As telas são medidas sem janela, numa área de 1920x1080, com `createComponentSnapshot`:
- "pintar inteira" é a tela toda;
- "coluna da agulha" é uma faixa de 5 px na altura toda, o que a agulha repinta a cada quadro;
- "uma linha" é uma faixa de 40 px na largura toda, o que um clique num passo do rack repinta.

Cada número é a mediana de 5 a 15 execuções, numa máquina com 7,6 GB de RAM e outro build rodando ao lado. Vale a ordem de grandeza e a comparação, não o milissegundo.

O tempo até a janela é medido por fora, com `--screenshot`: o tempo até o processo ganhar a janela principal.

## O que estava lento, e por quê

### 1. Cada comando custava uns 3 segundos num projeto grande

Todo comando que muda o projeto guarda o projeto em JSON duas vezes: antes e depois, para o desfazer. O `wnl::toJson` levava 1,6 s com 154 KB.

A causa principal é o `.replace ("\r\n", "\n")` no fim. O `juce::String::replace` refaz o texto inteiro a cada troca e procura a próxima do começo. Com 8 mil linhas, o custo fica quadrático. A árvore de `juce::var` (um objeto por passo e por nota, cada chave um `juce::Identifier` no pool global) somava o resto.

O mesmo `toJson` também roda:
- no fim de cada gesto (arrastar nota, bloco ou fader);
- no autosave, a cada 2 minutos: a tela parava 1,6 s;
- ao salvar;
- na assinatura do congelar, a cada remontagem do motor, uma vez por trilha congelada.

### 2. A agulha andava aos trancos

A agulha, os medidores e o visor andavam num `Timer` de 30 Hz. No Windows ele chega em intervalos de 31 ou 47 ms. A posição do transporte também só anda a cada bloco de áudio (~10 ms). Resultado: passos desiguais na tela.

Cada quadro da agulha repintava uma coluna de 5 px, mas o `paint` desenhava tudo:
- todos os blocos e todas as prévias de padrão;
- os nomes das raias, o teclado, a régua e a força;
- recortado, sem aparecer, mas custando do mesmo jeito.

As grades não eram opacas, então a janela pintava também as telas de trás. Na playlist, isso dava 7 ms por quadro.

### 3. Editar durante a música refazia os clipes do canal

Qualquer mudança num canal (um passo, uma nota, um bloco movido) apagava e recriava todos os clipes MIDI e de áudio dele.

O Tracktion reconhece o clipe pelo id ao montar o grafo novo. Com um clipe novo, o nó novo não acha a lista de notas ativas do antigo. A nota que estava soando (o 808, um pad) perde o par e o que cruza a agulha é disparado de novo.

Mexer em passo, nota ou bloco refaz o grafo de qualquer jeito: o Tracktion refaz a cada nota acrescentada. O volume não refaz.

### 4. Congelar renderizava de novo a cada clique (achado da revisão de 22/10)

A assinatura de uma trilha congelada era o JSON de um projeto com todos os padrões e a playlist. Qualquer passo em qualquer canal mudava a assinatura, e o motor renderizava de novo cada trilha congelada, na thread da tela: uns 500 ms por trilha, a cada clique. Num arraste, isso acontecia a cada 30 ms. O app sugere congelar quando a CPU passa de 85%, então quem segue a sugestão é quem mais sente.

### 5. Outras

- **Mixer:** o mixer inteiro repintava a cada mudança do projeto. Cada trilha tem texto, fader e knob.
- **Channel rack:** repintava inteiro a cada clique num passo, uns 15 ms a 1920x1080.
- **Prévias da playlist:** a prévia de cada bloco era desenhada retângulo por retângulo.
- **Remontagem do motor:** no modo SONG, ela calculava as notas de cada bloco de novo, para cada canal: 150 blocos vezes 28 canais.
- **Medidores:** o gradiente era montado duas vezes por quadro em cada medidor, e a queda contava quadros, não tempo.

## O que mudou

| Onde | O quê |
|---|---|
| `WnlFormat.cpp` (núcleo) | `toJson` escreve o texto direto, sem árvore nem `replace`. A saída é a mesma, byte a byte: o caminho antigo ficou como `toJsonReference` e o autoteste compara os dois. O `keepOlderSchema` não relê o arquivo inteiro quando o começo já diz o esquema atual. |
| `Projector.*` (núcleo) | Os clipes ficam: o mesmo conteúdo fica com o mesmo clipe, o clipe MIDI que mudou é reescrito no lugar (o mesmo id) e só se cria ou apaga quando o número muda. As notas de um bloco são calculadas uma vez por padrão, tamanho e começo. |
| `src/ui/Fluid.*` (novo) | `FrameClock` (VBlank), `PlayheadSmoother` (a agulha entre os blocos de áudio), `Ramp` (easing) e `needsPaint`. |
| `MainComponent.*` (núcleo) | Agulha, medidores e visor no VBlank, com a agulha suavizada. O `Timer` de 30 Hz fica para o resto, e como reserva sem VBlank. |
| `Playlist.cpp`, `PianoRoll.cpp`, `ChannelRack.*` (Design) | Grades opacas. O `paint` pula o que está fora da parte suja. O rack repinta só as duas colunas da agulha e só as linhas que mudaram. A prévia dos blocos saiu numa lista por repetição (lote do Direct2D) e voltou ao desenho de antes na fatia seguinte (ver "Ficou de fora"). Só repinta os blocos que começam ou param de tocar. |
| `MixerView.*`, `LevelMeter.*` (Design) | A trilha repinta só quando o desenho dela muda. O medidor repinta só quando um pixel muda. O gradiente fica guardado. A queda conta segundos (igual a 30, 60 ou 144 Hz). |
| `Projector.*`, `AppController.cpp` (núcleo; o congelar é da Gravação e automação) | A assinatura do congelado é um `FreezeKey`, com só o que chega na trilha: os canais que saem por ela e os clipes que eles tocam, as trilhas que chegam nela, os efeitos e as curvas. Ela é comparada com `==`, sem JSON. Com janela, a trilha cujo conteúdo mudou toca ao vivo na hora, e o render sai 1 s depois da última mudança, pelo `Housekeeping`, fora de gesto. Sem janela (autoteste, IA), segue na hora. |
| `CoreCommands.cpp`, `KeyMap.cpp`, `MainComponent.cpp` (núcleo) | Comando `pattern.clone` e as teclas do FL para o beat rápido: F4 (próximo padrão vazio), Ctrl+Shift+C (clonar) e + e - do teclado numérico (trocar de padrão). |

O renderer: o JUCE 8.0.13 já cria a janela com o Direct2D (`createNewPeer` usa o renderer 1, o D2D, fora do Wine). O app não troca isso. OpenGL não entrou: nada mediu ganho.

## Números

Antes: a base 1177d2a, no build de 21/10 17:48 (autoteste 555 ok). Depois: os builds de 22/10 09:41 (573 ok) e 09:57 (577 ok, saída 0, com o congelar e a prévia de volta).
A máquina estava mais folgada no segundo: o snapshot vazio de 1920x1080 caiu de 67 para 45 ms sem mudança nenhuma.
Por isso, nas telas, só valem as diferenças grandes.

| Medida | Antes | Depois |
|---|---|---|
| `toJson` do projeto grande (154 KB) | 1.584 ms | **0,91 ms** (no mesmo build, o caminho antigo levou 1.134 ms: 1.750 vezes) |
| Montar o projeto grande num gesto (2 `toJson` no fim) | 1.726 ms | 166 ms |
| Ler o JSON (`fromJson`) | 7,9 ms | 5,7 ms |
| Salvar | 1.549 ms | **14,4 ms** |
| Abrir o projeto grande com outro aberto antes | (não medido) | 19 ms |
| `steps.set` (PAT) | 3.234 ms | **2,4 ms** (refaz o grafo) |
| `pianoroll.addNotes` (PAT) | 2.837 ms | **3,2 ms** (refaz o grafo) |
| `mixer.setVolume` | 3.129 ms | **2,8 ms** (não refaz o grafo) |
| `steps.set` (SONG) | 3.023 ms | 4,7 ms |
| Mover bloco (SONG) | 3.235 ms | **5,7 ms** |
| Remontar o motor sem mudança | 5,1 ms | 2,3 ms |
| Clipes do 808 que ficam com o mesmo id (passo, nota ou bloco movido, no SONG) | 0 de 150 | **150 de 150** |
| Playlist: pintar inteira / coluna da agulha | 41,6 / 7,3 ms | 35,6 / **3,0 ms** (com o lote da prévia, que saiu: 23,0 / 3,3) |
| Piano roll: pintar inteira / coluna | 13,7 / 2,0 ms | 12,8 / 1,2 ms |
| Channel rack: pintar inteira / coluna | 15,6 / 2,6 ms | 15,6 / 1,1 ms |
| Channel rack: o que um clique num passo repinta | o rack inteiro (15,6 ms) | uma linha (**1,4 ms**) |
| Mixer: pintar inteiro / coluna | 19,5 / 1,8 ms | 13,7 / 1,1 ms |
| Agulha: a maior diferença entre o que andou num quadro e o que devia (140 BPM, blocos de 11,6 ms) | 25 ticks, a 30 Hz e em intervalos de 31 ou 47 ms | **5 ticks**, a cada quadro da tela |
| 20 `steps.set` no Hat com o Kick congelado | um render do Kick a cada passo (~500 ms cada, pela assinatura antiga) | **4,7 ms** os 20 (5,2 ms sem congelar) |
| Mexer no Kick congelado, com janela | um render a cada clique | 0,8 ms: toca ao vivo, e o render sai 1 s depois de parar |
| Até a janela, com o projeto grande (`--screenshot`, 3 vezes) | 5,1 / 3,2 / 3,2 s | 3,3 / 3,2 / 3,2 s (igual: a abertura não passa pelo `toJson`) |

Os cliques do beat de 1 minuto, depois:
- soltar um som no rack, com a nota achada no arquivo: 52 ms (a detecção do tom);
- soltar um áudio na playlist: 0,5 ms;
- prévia de um som do kit: 0,3 ms; de um áudio de 40 s: 6,7 ms (decodifica até 30 s antes de tocar).

## O que não foi testado

- A fluidez sentida com o mouse e a agulha numa tela de verdade. O VBlank só roda com janela: o autoteste mede o cálculo da agulha, e a captura só prova que a janela abre e fecha com ele.
- O som durante a edição: não foi ouvido. O autoteste prova que os clipes ficam com o mesmo id, e é por ele que o Tracktion segue a nota que estava tocando. O tempo de refazer o grafo do Tracktion não foi medido: o `createNodeForEdit` não sai no cabeçalho público.
- Falhas de buffer reais: o autoteste roda sem placa de som.
- Telas de 144 Hz: a queda dos medidores conta segundos, mas não foi vista.

## Ficou de fora

- **Desenhar as prévias dos blocos em lote** (`fillRectList`, o lote de sprites do Direct2D): a pintura inteira da playlist cai, mas o lote encaixa as bordas no pixel e as marcas perdem a suavização. O desenho mudava, então voltou ao de antes: a grade da playlist sai igual à da base, pixel a pixel, na captura. A decisão é do Design.
- **O `refresh` da playlist ainda repinta tudo** a cada mudança: arrastar um bloco repinta 1920x1080 (uns 25 ms). Um diff dos blocos, como o das linhas do rack, deixaria o arraste em poucos ms. É a mesma região que a Gravação de voz mexe.
- **A prévia do navegador** decodifica até 30 s de uma vez: 7 ms num WAV de 40 s, mais num MP3 longo. Tocar o começo enquanto o resto decodifica fica para depois.
- **O render do congelado fora da thread da tela:** o `AudioTrack::freezeTrack` do Tracktion renderiza o Edit ao vivo, então ele roda na thread da tela, uma vez por trilha depois que a pessoa para. Para tirar dali, o congelar teria de renderizar uma cópia (o caminho do `Exporter`) e trocar o arquivo. Fica para a dona do congelar.
- **O pisca do REC** (`TransportBar` e `MixerStrip`) ainda liga e desliga de uma vez. A `fluid::Ramp` está pronta para ele e para a logo ao abrir.
