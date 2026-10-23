# Fatiar o beat, o 808 e os kits

Para MCs e produtores: o 808 de um kit entra como o nativo, a nota longa não para no fim do sample,
um beat vira fatias no piano roll e uma pasta de kit vira um rack inteiro. O comportamento foi estudado
só por manuais e vídeos públicos de outras DAWs; o código é da WHYNOT.

## O 808 do kit

- Arraste um WAV ou MP3 para o channel rack (do Explorer ou do navegador de sons), ou use
  `channel.addSampler`. Se o som for um 808, o canal já nasce **mono, com glide de 80 ms e respeitando a
  duração da nota**, como o 808 do kit da WHYNOT. A barra de status avisa: "entrou como 808".
- Como o app sabe:
  - **pelo nome** do arquivo, do canal ou da pasta: "808", "bass", "baixo", "sub", "grave" ("808s/Dark.wav"
    também vale). As palavras de bateria vêm antes: "808 Clap" é palma e "Kick 808" é kick;
  - **pelo som**, quando o nome não diz: grave (60% da energia abaixo de ~150 Hz), longo (0,45 s ou mais),
    ainda forte 300 ms depois do ataque e com a nota abaixo do Mi 3. Um kick também é grave, mas some antes.
- Para desligar: `"detectRole": false` no comando, ou desmarque Mono e "Respeitar a duração da nota" no
  botão direito do canal.

## A nota mais longa que o sample (laço)

- No botão direito do canal: **Ajustes do sampler**. O grupo **LAÇO** liga o laço: enquanto a nota soa, a
  leitura volta do fim do laço para o começo dele, com um crossfade para a volta não estalar.
- Ao ligar pela primeira vez, o app **acha o laço sozinho** no corpo do som: depois do ataque e da queda de
  tom, onde o volume ainda está perto de -12 dB do pico, com as pontas em cruzamentos de zero e o tamanho em
  períodos inteiros da nota. **Achar laço** refaz isso.
- Na onda do painel, o trecho laranja é o laço: arraste as pontas, ou o meio para mover as duas juntas. Os
  knobs Começo, Fim e Crossfade fazem o mesmo com números.
- Num canal one-shot, soltar a nota sai do laço e o sample toca até o fim. Num 808 (respeitando a nota), a
  nota solta sai pelo release.
- Comando: `channel.setSampler { channel, loop, loopStart, loopEnd, loopXfadeMs, loopAuto }`. As pontas são
  frações do sample (0 a 1).

## Fatiar um beat

- **Novo canal fatiado:** no channel rack, **+ › Fatiar um beat ou loop**; ou no navegador de sons, botão
  direito no arquivo › **Fatiar no channel rack**.
- **Um canal que já existe:** botão direito no canal › **Fatiar**: pelas batidas (suave, médio ou forte) ou
  pela grade (1/4, 1/8, 1/16).
- Cada fatia toca numa tecla, **da grave para a aguda a partir do C4**, no tom original e só até a fatia
  seguinte (com um fade de 4 ms no fim). A fatia começa num cruzamento de zero, então o canal fatiado fica
  sem a rampa de ataque (o estalo da batida sai inteiro). As notas vão para o piano roll do padrão atual, uma
  por fatia, no tempo em que a batida soa no beat. Mudando o BPM do projeto, as fatias acompanham.
- O BPM do beat vem do nome do arquivo ("Loop 92bpm"), ou do tamanho (meio a 16 compassos, o mais perto do
  BPM do projeto), ou do projeto.
- **Mandar as fatias para o piano roll** reescreve as notas; **Tirar as fatias** volta o sample inteiro.
- Comandos: `channel.addSlicer { path, mode, sensitivity, division, bpm, key, toPianoRoll }`,
  `channel.slice { channel, ... }` e `channel.slicesToPianoRoll { channel }`. As fatias ficam no `.wnl` como
  campos opcionais do canal (sem esquema novo).

## Picotar um clipe de áudio

- Na playlist, a setinha no cabeçalho do clipe de áudio abre o menu: **Picotar › Pelas batidas, A cada tempo,
  A cada compasso**. O clipe vira um bloco por pedaço, cada um continuando de onde o anterior parou (como a
  ferramenta Cortar). Ctrl+Z desfaz tudo.
- Comando: `playlist.chopItem { item, mode: attack | beat | bar, sensitivity }`.
- Um WAV com andamento no nome ("Beat 140bpm.wav") toca como o arquivo é: o Tracktion lia o andamento no
  nome e o export da música travava. A correção é do `extra-onda1` (abde694), que este branch já traz.

## Kits

- **Uma pasta inteira:** arraste a pasta do kit para o channel rack (do Explorer ou do navegador), ou no
  navegador, botão direito na pasta › **Adicionar o kit ao channel rack**. Vira um canal por som (até 64), na
  ordem da bateria: kicks, caixas, palmas, chimbais, abertos, percussão, pratos, 808, efeitos, vozes, loops.
  - O papel de cada som vem do nome dele e das subpastas (Kicks, Snares, Hats, 808s...); o nome da pasta do
    kit não conta ("Trap 808 Kit" não faz de tudo um 808). Sem nome que diga, o som grave e longo vira 808.
  - O 808 nasce mono, com glide e respeitando a nota; o chimbal fechado e o aberto vão para o **grupo de
    corte 1** (o fechado corta o aberto), e o aberto corta a si mesmo.
  - Comando: `channel.addKit { folder, max }`.
- **MP3 de kit:** o codificador de MP3 põe uns 25 ms de silêncio no começo, o que atrasaria o one-shot. O app
  corta esse começo na leitura: com a etiqueta do LAME (a maioria dos MP3), o corte é exato; sem ela, corta o
  silêncio do começo, até 50 ms. Só vale para o sampler; o clipe de áudio da playlist toca o arquivo como ele é.
- **Grupo de corte e swing:** no botão direito do canal, **Grupo de corte** (nenhum, 1 a 8) e **Swing** (sem
  swing, 25%, 50%, 66% ou 75%). Antes eram só por comando (`channel.setChoke`, `channel.setSwing`).
