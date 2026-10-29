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

## Passada de 29/10: áudio, voz picotada e o motor (frente Desempenho)

Branch `extra-desempenho` (de `teste-integracao-2810` 7c5da8f). Duas medidas novas:

- **`whynot_rt_bench`** (`tests/perf/bench_rt.cpp`, sem JUCE). Mede o custo por bloco de 256 amostras a 48 kHz (orçamento de 5333 µs):
  - os 6 efeitos novos, a afinação, EQ, compressor, reverb, delay e limitador;
  - as vozes do sampler, com e sem o bend do canal (`ChannelPitch`);
  - o Synth 3 OSC.

  Para cada caso dá a mediana, a média, o p99 e o pior bloco, com o som e o silêncio à parte (é na cauda que os denormais aparecem). Também conta as alocações dentro do `process`. O ctest roda `whynot_rt_bench --check`, que falha se algum `process` alocar. Sem argumento, imprime a tabela; `--csv <arquivo>` grava os números.
- **Seção "Desempenho: voz picotada" do autoteste** (`src/app/selftest/HeavyProject.cpp`). Monta 300 blocos de voz sobrepostos em 3 raias (metade com pan, o que leva à cópia estéreo do `blockfile::stereoOf`) e 2000 notas num canal mono. Depois mede o motor, os crossfades automáticos, a playlist e o piano roll.

### O que estava lento

1. **O crossfade automático era O(n²) vezes as raias.** O `playlistedit::autoCrossfade` compara um bloco com todos os outros e, para cada um, percorre as raias (`laneSounds`). A playlist (`refresh`, a cada mudança do projeto e a cada passo de um arraste) e o motor chamavam essa conta bloco a bloco. Com a voz picotada, só ela levava 19 ms.
   - Agora as duas chamam `playlistedit::autoCrossfades`, que calcula todos de uma vez: agrupa por raia, ordena pelo início e olha só os vizinhos que podem se sobrepor.
   - O resultado é o mesmo bit a bit. O autoteste compara os dois na voz picotada, num projeto torto (mudos, blocos longos, inícios iguais, camadas, padrões no meio) e com uma raia em solo.
2. **O motor perguntava ao disco por bloco, a cada sync.** Para cada bloco de áudio, o `Projector` fazia de 2 a 5 consultas: se o arquivo existe, o arquivo invertido ou refeito e, com pan, a data do original e da cópia estéreo. Com 300 blocos do mesmo arquivo, eram centenas de consultas por mudança do projeto.
   - Agora cada resposta é guardada pelo caminho, dentro de um mesmo sync. O arquivo invertido e o refeito são calculados uma vez por canal; o `anyEdited` das notas afinadas, também.
   - No sync seguinte ele pergunta de novo, então um arquivo que sumiu continua sendo avisado na hora.
3. **O piano roll de um canal mono copiava e ordenava todas as notas a cada quadro da agulha**, para desenhar as curvas de glide, mesmo sem nenhuma nota com slide. Agora pula quando não há slide.

### Números (medianas; máquina com 7,6 GB e outras sessões abertas)

| Medida | Antes (7c5da8f) | Depois |
|---|---|---|
| Crossfades de todos os blocos (300 de voz) | 19,5 ms (um a um) | 0,12 ms (de uma vez) |
| Playlist com a voz picotada: atualizar | 17,0 ms | 0,34 ms |
| Playlist com a voz picotada: pintar inteira / coluna da agulha | 79,4 / 3,73 ms | 62,3 / 2,20 ms (ruído; o paint não mudou) |
| Motor: mover um bloco da voz e reconciliar (SONG) | 27,4 ms | 1,18 ms |
| Motor: reconciliar sem mudança | 0,31 ms | 0,18 ms |
| Piano roll mono, 2000 notas: pintar inteira / coluna | 12,1 / 1,56 ms | 11,2 / 1,41 ms |
| Banco: projeto pesado (16 synths 3 OSC com os 6 efeitos novos e a afinação), um núcleo | 2350 µs (44%) | sem mudança no áudio; a mesma conta deu 2785 µs (52%) noutra rodada, a margem do banco nesta máquina |

O som não mudou. Os `--render` do antes e do depois saíram iguais amostra por amostra (só o bloco `data`; a data do `bext` muda a cada render) em seis projetos: o Demo 3.2, o Demo Synth, o Grande da Fluidez (em PAT e em SONG) e o Pesado (em PAT e em SONG, com os 300 blocos de voz tocando, pico 0,88).

### A thread de áudio (revisão dos itens novos)

- **Alocações:** nenhuma dentro do `process` nos 17 casos do banco. O `ChannelPitch`, o `SampleVoice`, o `PianoVoice`, o clipper, o lo-fi, o chorus, o flanger, o phaser, o EQ dinâmico e a afinação (PSOLA) usam só memória preparada no `prepare`.
- **Travas e I/O:** nenhuma no tempo real. A trava do WHYNOT Piano (`streamLock`) só vale no render offline, de propósito: ali ele espera o anel em vez de deixar faltar.
- **Denormais:** todos os `process` ligam FTZ/DAZ (`DenormalGuard` nos efeitos e no Synth; `ScopedNoDenormals` no `WhynotFx`, no sampler e no piano). No banco, o silêncio depois do som custa o mesmo que o som.
- **O que pesa:** as vozes do sampler (cerca de 10 µs por voz e por bloco, mais uns 50% com bend e vibrato, que recalculam a razão de leitura com `exp2` a cada amostra), o limitador e o clipper. Os dois últimos sobreamostram 4x em dois meios-banda de 63 e 29 coeficientes, e o clipper ainda chama `tanh` acima do joelho. Reorganizar a sobreamostragem com o mesmo resultado bit a bit não ganhou nada no experimento: a soma em cadeia é o limite. Para ganhar ali, o som mudaria (somas em paralelo, SIMD entre canais, `tanh` aproximado). Isso fica para quem é dono dos efeitos decidir, com o ouvido.

### Ficou de fora

- O `refresh` da playlist ainda remonta as prévias de todos os padrões e repinta a tela inteira a cada mudança (achado 1 da Fluidez). Um cache das prévias por padrão é o próximo passo.
- O `mixer.setVolume` ainda leva a um sync inteiro do motor (as specs de todos os canais, sem refazer o grafo).
- O desenho das notas das prévias dos padrões percorre todas as repetições até a borda, mesmo na coluna da agulha.
- Não ouvido e sem ASIO: as medidas são do render offline e do banco. A carga da placa ao vivo não foi medida.
