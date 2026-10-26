# Como compilar e abrir a WHYNOT LAB

Estado: entrega 3.4 (acabamento), o fim da Fase 3. Testado no Windows 11 com VS 2022 Build Tools 17.14, CMake 4.4 e Ninja 1.13.

## 1. O que precisa estar instalado

| Ferramenta | Versão testada | Como instalar |
|---|---|---|
| Visual Studio 2022 Build Tools, workload "Desenvolvimento para desktop com C++" | 17.14 | `winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"` (o `--override` já escolhe o workload C++) |
| CMake | 4.4.4 | `winget install --id Kitware.CMake -e` |
| Ninja | 1.13.2 | `winget install --id Ninja-build.Ninja -e` |
| git | 2.55 | `winget install --id Git.Git -e` |

## 2. Baixar as dependências (uma vez)

Na pasta do projeto:

```
git submodule update --init external/tracktion_engine
git -C external/tracktion_engine config submodule.modules/juce.url https://github.com/juce-framework/JUCE.git
git -C external/tracktion_engine submodule update --init --depth 1
```

O segundo comando troca o endereço do JUCE de SSH para HTTPS, para não precisar de chave SSH do GitHub.

### 2b. MP3 e ASIO (opcional, fora do git)

O app compila sem estes dois. Sem o LAME, o Exportar MP3 explica que o build não tem MP3. Sem o ASIO SDK, a lista de drivers fica só com o WASAPI. Para ligar os dois, baixe das fontes oficiais e extraia em `external/` (essas pastas estão no `.gitignore`). No PowerShell, na pasta do projeto:

```
New-Item -ItemType Directory -Force external, $env:TEMP\wnl | Out-Null
curl.exe -L -o $env:TEMP\wnl\lame-3.100.tar.gz https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download
curl.exe -L -o $env:TEMP\wnl\asio.zip https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
Get-FileHash $env:TEMP\wnl\lame-3.100.tar.gz, $env:TEMP\wnl\asio.zip
tar -xzf $env:TEMP\wnl\lame-3.100.tar.gz -C external
Expand-Archive $env:TEMP\wnl\asio.zip $env:TEMP\wnl\asio
Move-Item $env:TEMP\wnl\asio\ASIOSDK external\asiosdk
```

Confira os hashes antes de extrair:

| Arquivo | sha256 |
|---|---|
| `lame-3.100.tar.gz` | `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e` |
| `ASIO-SDK_2.3.4_2025-10-15.zip` | `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca` |

Tem de ficar `external\lame-3.100\include\lame.h` e `external\asiosdk\common\iasiodrv.h`. Depois, rode de novo o primeiro comando da seção 3 (o `cmake -S . -B build`). A mensagem do CMake diz se achou cada um, e o primeiro build depois disso é cheio (de 15 a 20 min). As licenças estão em `docs/licencas.md`: o LAME é LGPL e o ASIO SDK é usado pela GPLv3.

## 3. Compilar

Abra o "x64 Native Tools Command Prompt for VS 2022" (ou rode `vcvars64.bat`) e, na pasta do projeto:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target whynot_test_plugins whynot_lab whynot_audio_tests whynot_dsp_tests -j 3
```

Os alvos `whynot_audio_tests` e `whynot_dsp_tests` são os testes do `ctest` (seção 10). O alvo `whynot_test_plugins` compila quatro plugins VST3 próprios em `build\test_plugins` (um efeito, um instrumento e dois quebrados de propósito), usados pelo autoteste. Para pular: `-DWHYNOT_BUILD_TEST_PLUGINS=OFF` no primeiro comando.

- O primeiro build leva de **15 a 20 minutos** nesta máquina com `-j 3`, porque compila o JUCE, o Tracktion, o LAME e o ASIO. Com a RAM apertada e `-j 2`, já levou 37 minutos. Os seguintes levam de segundos a poucos minutos.
- Use **no máximo 3 jobs** (`-j 3`). Com 7,6 GB de RAM a memória livre chega a zero no pico; feche o navegador antes do primeiro build. Se travar, use `-j 2`.

## 4. Abrir

```
build\whynot_lab_artefacts\Release\WHYNOT LAB.exe
```

Para abrir um projeto direto: `"WHYNOT LAB.exe" "C:\caminho\MeuBeat\MeuBeat.wnl"`.

O app abre com o **kit WHYNOT** já no channel rack: kick, clap, hat, open hat, 808 e Bell (um sino para melodias), todos sintetizados pelo próprio app. O 808 vem em mono com glide e com a raiz em Dó 2, e o Bell em Dó 5: a nota que aparece no piano roll é a que soa.

Em cima ficam as abas **Playlist (F5)**, **Channel rack (F6)** e **Piano roll (F7)**, uma por vez; o **mixer (F9)** fica embaixo. Em tela baixa (notebook), o mixer sai sozinho quando você abre o piano roll ou a playlist e volta quando você volta ao channel rack; F9 traz ele de volta a qualquer hora.

## 5. Primeiro beat

1. Clique nos passos para ligar; arraste para pintar; botão direito apaga.
2. **Ctrl + roda do mouse** sobre um passo muda a força (velocity); **Alt + roda** faz roll (2, 3, 4, 6 ou 8 batidas no passo).
3. **Barra de espaço** toca e para. O padrão repete enquanto o loop (botão ao lado do metrônomo) estiver ligado.
4. Arraste o BPM para cima ou para baixo (Shift: ajuste fino), use a rodinha, ou dê duplo clique para digitar.
5. Arraste samples do Explorer para o channel rack para criar canais. Solte em cima do nome de um canal para trocar o sample dele. O app **acha a nota do sample** sozinho (808, baixo, melodia) e a barra de status mostra qual foi; bateria sem nota definida fica em C4 (60).
6. Clique na saída de um canal ("→ Kick") ou de uma trilha do mixer para mudar para onde o som vai.
7. Botão direito no nome do canal: ajustes do sampler, renomear, trocar sample, **cortar a si mesmo** (nota nova corta a anterior, ligado no 808 e no open hat) e remover. **Duplo clique no nome** abre os ajustes do sampler (seção 5b).
8. **Ctrl+S** salva. O projeto vira uma pasta com o `.wnl`, a subpasta `samples/` (cópias dos samples) e `renders/`.
9. **Ctrl+R** abre o Exportar já em WAV (48 kHz, 24 bits), com a pasta `renders/` sugerida. Por padrão segue o transporte: em **PAT**, o padrão 4 vezes; em **SONG**, a playlist inteira. Veja a seção 7c.

## 5b. Ajustes do sampler (afinação, envelope, queda de tom)

**Camadas por força** (bloco 4f, como os pads de uma bateria eletrônica): no painel do sampler, a linha de baixo, **+ Camada**, põe outro sample que toca quando a nota é mais forte (o kick suave e o kick forte). São até 3 camadas: a primeira começa na força 100, e um clique na camada escolhe a força mínima, troca o arquivo ou tira. Abaixo da primeira camada toca o sample do canal. As camadas usam a nota de raiz, a afinação, o envelope, o início e o laço do canal: servem para o mesmo som em outra força (o kick suave e o forte), não para outro instrumento. Cada canal do rack já é um pad: tem a trilha de mixer, os efeitos, o grupo de corte e o swing dele. Fatiar o canal tira as camadas (fatiado, cada tecla é uma fatia). Salvar com os samples copia as camadas para `samples/`. Pela IA: `channel.layers`, `channel.addLayer`, `channel.setLayer` e `channel.removeLayer`.

Duplo clique no nome de um canal do sampler (ou botão direito, "Ajustes do sampler") abre um cartão sobre o channel rack. Clique fora para fechar. Cada arrasto de knob é um passo de desfazer, e tudo vai para o `.wnl`.

| Controle | O que faz |
|---|---|
| **Raiz** + **Detectar** | Em que nota o sample soa. "Detectar" ouve o sample e acha a nota e os cents: um sample 30 cents acima toca afinado. Escolher a raiz à mão zera os cents |
| **Afinação** | Transpõe o canal em semitons, com fração (+0,50 st = 50 cents) |
| **Ataque, Decay, Sustain** | Envelope. Sustain 100% = sem decaimento. Sustain 0% com decay curto = o "decay" do 808: a nota some sozinha, mesmo segurada |
| **Queda** e **Tempo** | A nota começa tantos semitons acima e cai até ela (o "drop" do 808). Um slide não reinicia a queda |
| **Início** | Onde a leitura do sample começa: pula silêncio ou o ataque |

Para a IA: `channel.setSampler {channel, rootKey?, rootCents?, tune?, attackMs?, decayMs?, sustain?, pitchDrop?, pitchDropMs?, start?}` e `channel.detectRoot {channel, apply?}`. `channel.addSampler` e `channel.setSample` acham a nota sozinhos (`detectRoot: false` desliga; um `rootKey` dado vence).

## 6. Melodia e 808 com glide (piano roll)

1. Clique no nome de um canal no channel rack (ele fica destacado) e aperte **F7**. Também dá para clicar na miniatura de notas de um canal ou usar o menu do nome, "Abrir no piano roll". A lista no canto esquerdo do piano roll troca o canal.
2. **Desenhar (P)**: clique na grade cria uma nota com a duração e a força da última nota que você tocou; o mesmo arrasto já move a nota. Arraste a **borda direita** para mudar a duração. **Botão direito** apaga (arrastando, apaga várias).
3. **Pintar (B)**: arraste para encher de notas, uma a cada duração da última nota.
4. **Selecionar (E)**, ou **Ctrl+arraste** em qualquer ferramenta: caixa de seleção. Com notas selecionadas: **Delete** ou **Backspace** apaga, **Esc** limpa a seleção, **setas ↑↓** transpõem (Ctrl ou Shift: oitava), **setas ←→** andam um passo da grade, **Ctrl+A** seleciona tudo, **Shift+arraste** arrasta uma cópia.
5. **Grade**: passo, 1/2 e 1/3 de passo, tempo, compasso ou sem grade. Segure **Alt** enquanto arrasta para ignorar a grade.
6. **Glide do 808**: ligue **Slide** (ou aperte **S** sem nada selecionado) e desenhe a nota de destino. Ela desliza da nota que estava soando, e a curva branca mostra o caminho e o tempo do glide. Com notas selecionadas, **S** liga ou desliga o slide delas. O slide precisa do canal em **Mono**; se não estiver, o app liga e avisa na barra de status. **Glide** escolhe o tempo (0 a 600 ms).
7. **Força**: na faixa de baixo, arraste para cima ou para baixo sobre as hastes. Com seleção, só as notas selecionadas mudam.
8. **Tom**: tônica e escala do projeto. As linhas da grade seguem o teclado em qualquer escala: a da tecla preta escura e a da branca clara. A escala fica num fio na beira esquerda da grade: as notas dela marcadas e a tônica em laranja. As notas dos outros canais do padrão aparecem de fundo, em cinza.
9. Passos do channel rack aparecem no piano roll como notas e viram notas de verdade na primeira edição (um passo de desfazer só).
10. Clique no teclado à esquerda para ouvir a nota. **Roda** rola na vertical, **Shift+roda** na horizontal, **Ctrl+roda** dá zoom no tempo e **Alt+roda** muda a altura das linhas.
11. **Editar** (na barra do piano roll), como no FL. Vale nas notas selecionadas; sem seleção, em todas:
    - **Legato (Ctrl+L)**: cada nota vai até a próxima; as de um acorde vão juntas.
    - **Juntar (Ctrl+G)**: notas da mesma tecla que se encostam ou se cruzam viram uma.
    - **Mudo (Alt+M)** e **Tirar o mudo (Alt+Shift+M)**: a nota fica no piano roll, apagada, mas não toca nem sai no MIDI.
    - **Humanizar** (leve, médio ou forte): o tempo e a força mudam um pouco ao acaso.
    - **Strum** (leve, médio ou forte): nas notas que começam juntas, cada uma entra um pouco depois da de baixo, como um violão dedilhado; o fim fica no lugar.
    - **Capturar o que eu toquei agora (Capture MIDI)**: o app guarda o que você tocou no teclado-piano ou no controlador, mesmo sem gravar. A última frase (depois da última pausa de 2,5 s) vira notas do canal: tocada com o padrão rodando, cada nota fica onde caiu; parado, ela começa no início do padrão. Também é uma ação no editor de atalhos, sem tecla de fábrica. Comando: `record.captureMidi`.
    - **Levar para a escala**: cada nota fora do tom vai para a nota do tom mais perto.
    - **Encaixar na escala ao desenhar e mover**: desenhar, arrastar e as setas só caem em notas do tom.
    - **Carimbo de acorde**: escolha um acorde e cada clique do Desenhar põe o acorde inteiro, com o baixo na nota clicada. Os "da escala" usam as notas do tom (tríade, com 7ª, com 9ª); as formas fixas são maior, menor, quinta, sus2, sus4, 7ª e outras. **Esc** desliga. Enquanto o carimbo ou o encaixe estão ligados, um aviso fica no canto da grade.
12. **Importar MIDI**: arraste um `.mid` tipo 0 ou 1 (pacote de acordes, melodia) para o piano roll, ou use Arquivo > Importar MIDI ou Editar > Importar MIDI no piano roll. Um cartão pergunta quais trilhas entram (todas ou uma; no tipo 0, uma por canal MIDI), se as notas trocam ou somam ao que o canal toca no padrão atual, e se começam no começo do padrão ou no compasso da agulha (no PAT, tocando ou pausado com Ctrl+Espaço). Enter importa e Esc cancela. Tom, força e duração vêm convertidos para 960 ticks por tempo; uma nota de força 0 no arquivo é o fim da nota anterior. Compassos vazios no começo do arquivo saem, o padrão cresce até 16 compassos, e o app avisa se o arquivo foi feito em outro BPM. Um Ctrl+Z desfaz tudo. Pela IA: `pianoroll.inspectMidi` lista as trilhas e `pianoroll.importMidi` aceita `track`, `replace` e `at`.

## 6b. WHYNOT Piano

1. No channel rack, **+** → "WHYNOT Piano". O canal novo toca um piano de cauda gravado (o Salamander Grand Piano, 8 camadas de força) e ganha a própria trilha de mixer.
2. **Duplo clique** no nome abre o piano roll. **Botão direito** troca o timbre: **Cauda** (o piano como foi gravado), **Suave** (mais escuro e macio, soltura mais longa) e **Lo-fi** (abafado, quase mono e saturado, sem o ruído do martelo).
3. A nota respeita a força e a duração: soltar a tecla toca a ressonância das cordas e o martelo voltando. O pedal (CC64) de um teclado MIDI segura as notas até soltar o pedal.
4. A biblioteca fica em `%APPDATA%\WHYNOT LAB\Bibliotecas\Salamander` (592 MB). Sem ela, o canal fica vermelho com o motivo. Para montar de novo a partir do arquivo original: `python -I tools\piano\salamander_para_whynot.py <pasta extraída> "%APPDATA%\WHYNOT LAB\Bibliotecas\Salamander"`.
5. Para a IA: `channel.addPiano`, `piano.setPreset`, `piano.listPresets` e `piano.status`. Os créditos estão em Arquivo → "Sobre a WHYNOT LAB".

## 6c. Tom do beat e dicas do 808

1. Ponha o beat (WAV ou MP3) na playlist ou num canal de áudio. Um sample de 8 s ou mais num sampler também conta. O app ouve os primeiros 2 min 30 s numa thread de fundo, sem travar a tela, e em cerca de 1 s a barra de status mostra, por exemplo, "Tom do beat: Lá menor (84%)". Abaixo de 50% ele diz "talvez".
2. **Usar no projeto** muda o tom do projeto, e o piano roll marca as notas da escala no fio da beira da grade. **Agora não** esconde o aviso. Com o mouse em cima do aviso aparecem as 3 alternativas e o relativo: Lá menor e Dó maior usam as mesmas notas, então a escala e as dicas servem para os dois. Se o tom do projeto já é o do beat, só aparece uma mensagem.
3. No piano roll, **Dicas do beat** liga e desliga as dicas. O botão só aparece com um beat analisado no projeto. Em cada compasso, a linha mais clara é a fundamental do acorde do beat, onde o 808 casa, e ela fica mais clara ainda de Dó 1 a Si 2. As linhas mais fracas são a terça e a quinta. O nome do acorde fica na régua, com "?" quando a confiança é menor que 50%.
4. O padrão conta a partir do primeiro bloco dele na playlist. Sem bloco, ele conta junto com o beat.
5. Os acordes por compasso usam o BPM do projeto: acerte o BPM para o do beat antes. É uma estimativa, só com tríades maiores e menores: confira de ouvido.
6. Para a IA: `audio.detectKey { channel | path, start?, wait? }` devolve o tom, a escala, a confiança, a afinação em cents, as alternativas, o relativo e `bars`, com o acorde, a fundamental e a confiança de cada compasso. O campo `setKey` da resposta vai direto no `project.setKey`.
7. Teste sem JUCE: `whynot_key_tests` mede a taxa de acerto em áudio sintético. `whynot_key_tests --wav <arquivo.wav> <bpm>` analisa um WAV seu.

## 7. Arranjar (playlist e modo song)

1. **F5** abre a playlist. "Clique coloca" mostra o que um clique na grade coloca: o padrão escolhido no transporte, ou o último bloco em que você clicou.
2. Clique numa raia para colocar o bloco, encaixado no compasso. Arraste para mover (inclusive para outra raia); arraste a **borda direita** para mudar a duração: um padrão mais longo que o bloco repete. **Shift+arraste** copia, **botão direito** apaga, **Delete** apaga o bloco selecionado.
3. Arraste arquivos de áudio do Explorer para a playlist: cada um vira um clipe com a forma de onda, num canal de áudio próprio (com trilha de mixer). No clipe de áudio as duas bordas aparam: a esquerda corta o começo do arquivo.
4. **Duplo clique** num bloco de padrão abre esse padrão no channel rack. Duplo clique no nome de uma raia renomeia.
5. **PAT / SONG** (no transporte, ou a tecla **L**): PAT toca o padrão atual em loop; SONG toca a playlist. Em SONG, o bloco que está tocando ganha uma marca laranja em cima.
6. **Marcadores** (intro, verso, refrão): botão direito na régua > Marcador aqui, ou **Alt+T** na agulha. Clique no marcador leva a agulha para ele; **duplo clique** seleciona o trecho até o próximo marcador (vira o loop e o trecho que o Exportar oferece); arraste para mover; botão direito renomeia ou apaga.
7. **Tap tempo**: clique no **TAP** do transporte no ritmo da música (4 vezes ou mais). O BPM segue as batidas; uma pausa de 2 s fecha a conta, e as batidas seguidas são um passo de desfazer só.
8. **Warp do loop**: solte um loop (até 32 s) na playlist, e ele entra no BPM do projeto sem mudar o tom, pelo BPM do nome do arquivo ("Loop 90bpm.wav") ou pela análise. Entre a metade e o dobro, vale o mais perto do projeto (um loop de 70 num projeto de 140 não estica). O beat inteiro continua acertando o BPM do projeto quando chega num projeto vazio. A setinha do bloco tem "Esticar para o BPM do projeto" e "Voltar ao tempo do arquivo". Comando: `audio.warpToProject`.
9. **Pegar o swing de um loop** (setinha do bloco de áudio): mede o atraso dos contratempos de um loop de bateria, já no tempo do projeto, e põe o mesmo swing nos canais com passos do padrão atual. Comando: `audio.extractGroove`.
10. **Reamostrar** (botão direito na régua): a música inteira, ou o trecho selecionado, com o mixer e o master, vira um áudio no compasso depois do fim, numa raia vazia, para picotar, inverter ou esticar. Comando: `project.resample`.
11. **Renderizar no lugar** (botão direito no nome do canal, no channel rack): o que o canal toca na música vira um WAV, num canal de áudio novo na primeira raia vazia da playlist, e o canal original fica mudo. A música soa igual, e a CPU folga.
   - **Com os efeitos da trilha**: o áudio já sai com os efeitos, o volume e o pan da trilha do mixer; ele vai para uma trilha nova e limpa, que segue para o mesmo lugar e manda os mesmos envios (o reverb do bus continua ao vivo). Sobra um compasso no fim para o reverb e o delay acabarem.
   - **Seco**: só o instrumento; o áudio sai pela mesma trilha, e os efeitos dela continuam ao vivo.
   - O canal precisa tocar na playlist (no SONG). Ctrl+Z desfaz tudo num passo. Comando: `channel.renderInPlace` (`effects`, `mute`).
12. **Organizar as raias**: o ponto à direita do nome da raia é o mudo (cheio toca, vazado está mudo). **Ctrl+clique** nele põe a raia em **solo**: com alguma raia em solo, só as em solo tocam, e as outras ficam apagadas, com "fora do solo". O mudo sempre vence. O botão direito no nome da raia tem a **cor** (uma faixa na beira, para achar a voz, a bateria, o 808), mover para cima e para baixo (os blocos vão junto), renomear e a entrada para gravar. O mudo e o solo valem para tocar e para exportar. Comandos: `playlist.setTrack` (`color`, `muted`, `solo`) e `playlist.moveTrack`.

### Áudio para MIDI (bloco 4e)

Na setinha do cabeçalho de um bloco de áudio da playlist, **Virar MIDI**:
- **Bateria**: o beatbox do MC (ou um loop de bateria) vira um padrão novo no kit: cada batida ganha o papel pelo timbre (o grave é o kick, o agudo e curto é o chimbal, o resto é a caixa, ou a palma se o rack não tiver caixa), encaixada no 1/16 do BPM do projeto, com a força da batida.
- **Melodia**: uma voz ou um instrumento só (sem a bateria por baixo) vira notas no piano roll do primeiro synth ou plugin do rack (ou do 808), encaixadas no 1/16.
- O áudio fica como estava, e o padrão novo fica escolhido (Ctrl+Z desfaz). Pela IA: `audio.toMidi` (com `item` ou `path`, `mode` drums ou melody, `channel` e `quantize`).

## 7b. Plugins VST3

1. **F8** (ou "Plugins" no trilho da esquerda) abre o navegador de plugins.
2. **Pastas**: por padrão, `C:\Program Files\Common Files\VST3`, onde os instaladores colocam os VST3. "Adicionar pasta..." inclui outra (um plugin portátil, por exemplo).
3. **Escanear**: cada arquivo `.vst3` novo ou alterado é aberto num **processo separado**, com limite de 30 s. Plugin que trava ou derruba esse processo vai para **Bloqueados**, com o motivo, e o app continua de pé. Arquivo que não mudou vem do cache e não é aberto de novo.
4. **Instrumento**: arraste do navegador para o channel rack, ou dê duplo clique nele. O canal novo ganha uma trilha de mixer, como os samples. Passos e piano roll tocam o instrumento normalmente.
5. **Duplo clique no nome** de um canal VST3 abre a janela do plugin. Renomear passa para o menu do botão direito.
6. **Estado**: o que você ajusta no plugin vai para o `.wnl` ao salvar (e para o WAV ao exportar). Mexer num plugin conta como alteração não salva. O estado do plugin não entra no desfazer (Ctrl+Z), como no FL.
7. **Plugin que falta**: um projeto aberto num PC sem o plugin abre normalmente. O canal fica marcado em vermelho, e a dica diz qual plugin falta. Instale ou escaneie e o canal volta a tocar, sem reabrir o projeto.
8. **Se um plugin derrubar o app**: na próxima abertura ele vem bloqueado e a barra de status avisa. Para tentar de novo: Plugins, Bloqueados, "Desbloquear e escanear de novo".
9. **Efeitos VST3** vão nos inserts do mixer: no painel de inserts da trilha, "Adicionar efeito", seção "Plugins VST3", ou arraste o efeito do navegador para um slot. "Abrir" mostra a janela do plugin; o bypass e a ordem funcionam como nos efeitos nativos, e o estado do plugin também vai para o `.wnl`.

Para testar sem instalar nada no Windows, o Surge XT tem uma versão portátil oficial (`surge-xt-win64-…-portable-install.zip`, em github.com/surge-synthesizer/releases-xt). Extraia numa pasta e adicione essa pasta em Pastas.

## 7c. Exportar: WAV, MP3, MIDI e stems

Um diálogo só, como o render do FL. **Ctrl+R** abre em WAV, **Ctrl+Shift+R** em MP3 e **Ctrl+Shift+M** em MIDI; o menu Arquivo tem os quatro.

| Formato | O que sai |
|---|---|
| WAV | 48 kHz / 24 bits, com os efeitos do mixer e do master |
| MP3 | 44,1 kHz, codificado com o LAME: 128, 192, 256 ou 320 kbps (padrão 320), ou taxa variável (VBR V0). Leva o título do projeto na tag e toca sem silêncio a mais no começo nos players que leem o quadro do LAME |
| MIDI | Uma faixa por canal, com o nome do canal, mais o andamento e o compasso. As notas saem como o motor toca: passos viram notas, o padrão repete e o mono/slide do 808 vale. Canais de áudio ficam de fora |
| Stems | Um WAV por trilha do mixer, escolhendo quais. Vêm marcadas as que recebem som de um canal que toca. Cada stem é a saída da própria trilha, como no FL: o envio para o bus de reverb sai no stem do bus, não no da trilha. Por padrão os efeitos do master ficam de fora (volume e pan do master continuam), como nos stems para mixar ou masterizar em outro lugar. Assim, somando os stems de trilhas paralelas, dá a mixagem sem a cadeia do master. A caixa "Com os efeitos do master" (ou `masterChain: true` no comando) passa cada stem pela cadeia do master, sozinho |

"O que sai" escolhe entre **Padrão atual** (repetido 1, 2, 4, 8 ou 16 vezes) e **Música inteira** (a playlist até o fim do último bloco).

**Para vender** (a quinta aba, bloco 2 de 23/10) monta o pacote de quem vende beat numa ação só, em segundo plano. Escolha a pasta, e o app cria dentro dela uma pasta com o nome do beat, o BPM e o tom ("Noite 140bpm Cm"):
- o **WAV limpo**, 44,1 kHz / 24 bits, a música inteira, sem tag;
- o **MP3 com a tag**, 320 kbps: a sua tag de produtor (WAV ou MP3, até 30 s) toca logo no começo e depois a cada 15, 20, 30, 45 ou 60 s. A tag escolhida fica guardada para as próximas vezes ("Sem tag" tira);
- os **stems**, se marcados: um WAV por trilha do mixer que recebe som, na subpasta Stems;
- o **zip** da pasta, ao lado dela, pronto para mandar ao comprador.

Pelos comandos: `project.exportPack` (pasta, `wav`, `mp3`, `stems`, `zip`, `tag`, `tagEvery`, `tagGainDb`) e `export.setProducerTag` / `export.producerTag`.

## 7d. Navegador de sons (Alt+F8)

**Alt+F8**, ou "Sons" no trilho, abre o navegador no lugar do de plugins. As pastas:
- o Kit WHYNOT;
- a sua **Biblioteca**, em `Músicas\WHYNOT LAB\Biblioteca`, onde você põe drum kits, loops e one-shots;
- as pastas que você adicionar.

Como usar:
- **Clique** num som: ouve a prévia, por cima da música (as setas também).
- **Duplo clique** ou **Enter**: cria um canal.
- **Arraste** para o channel rack: cria um canal, ou troca o sample se soltar no nome de um canal.
- **Arraste** para a playlist: vira um clipe de áudio na raia e na posição soltas.
- A **busca** procura em todas as pastas.
- **BPM e tom**: cada som mostra o andamento e o tom ("120 BPM · Lá menor"), ou a nota de um one-shot (o 808 em "Fá"). Vêm do nome do arquivo ("Loop Am 140bpm", "C#min", "F minor", "Lá menor"), e, sem nome, da leitura do áudio em segundo plano. Um loop curto é repetido até dar o andamento, e o tamanho dele precisa fechar em tempos inteiros. Bateria de um toque só (kick, caixa, prato) não ganha nota. A dica da linha diz de onde veio cada um. No nome, a forma por extenso vence ("Loop Em Lá Menor" é Lá menor), e "AM" só vale num nome todo em maiúsculas ("LOOP_AM_120", Lá menor); "FM" e "GM" não são tom. A leitura fica guardada em `sons-analise.json`, na pasta de dados, e um arquivo que não mudou não é lido de novo. Um arquivo que só está na nuvem (o OneDrive com "Arquivos sob demanda") não é lido, porque abrir baixaria o arquivo: baixe a pasta antes, se quiser o BPM e o tom dele.
- **Filtro** (a nota musical no cabeçalho): mostra, de todas as pastas, só os sons com **as mesmas notas do projeto** (o mesmo tom ou o relativo; num modo, os tons dele: Ré dórico aceita Dó maior e Lá menor; e o one-shot cuja nota está na escala) ou que **combinam** (também os vizinhos, uma quinta acima e abaixo). O menu diz quais tons entram em cada um; com o **BPM perto do projeto** (4% para mais ou para menos, ou a metade e o dobro); e só **loops** ou só **one-shots**. Os sons ainda não lidos entram na fila e aparecem quando a leitura disser que combinam. A busca continua valendo junto. Pela IA: `browser.info` e `browser.find`.

## 7d2. Projetos (F11): ouvir sem abrir

**F11**, ou "Projetos" no trilho, mostra os seus projetos: os da pasta `Músicas\WHYNOT LAB` e os que você abriu ou salvou por último.
Só o projeto aberto fica carregado no motor. Os outros você ouve pela **prévia**, um FLAC que fica em `preview\<nome>.flac`, na pasta de cada projeto.

- **Ctrl+S** renderiza a prévia em segundo plano. Dar play na música pausa o render, que volta quando ela para.
- **Renderizar** faz a prévia do projeto escolhido; **Pendentes** faz a de todos os que não têm prévia ou estão desatualizados.
- O **▶** da linha toca a prévia no deck que soa, embaixo. **Shift+▶** põe no outro deck, alinhado com o que toca.
- **A/B:** os dois decks andam juntos e a troca (**Tab**, ou o A | B) é só de volume, em 20 ms.
  - **Igualar volume** (ligado) deixa os dois no mesmo LUFS: o mais alto desce até o outro, e a raia mostra quanto.
  - **Mesmo compasso** (o padrão) põe o outro deck no mesmo compasso, pelo BPM de cada projeto. Também há **Mesmo segundo** e **Livre**.
  - **Loop** repete 1, 2, 4, 8 ou 16 compassos a partir de onde está, nos dois decks.
  - **Referência** carrega no B uma música de fora (WAV, MP3, FLAC); o LUFS dela é medido ao carregar.
- Clique na onda de um deck para ir a um ponto (o outro vai junto); clique na letra para ouvir aquele deck.
- **Organizar:** a estrela da linha marca o favorito. O botão direito numa linha dá a cor, o status (Ideia, Em produção,
  Mix, Pronto) e as tags. Isso fica no `biblioteca.json` da pasta de dados, não no projeto.
- **Barra lateral:** Todos, Recentes, Favoritos, cada status, cada tag e as pastas. "Adicionar pasta" põe outra pasta na
  varredura (botão direito tira).
- **Busca:** texto livre (nome e tags) e `bpm:140-150` (acha também 70-75, o meio-tempo), `tom:Am`, `tag:sombrio`,
  `status:mix` e `fav`. As palavras se somam. Esc limpa.
- **Versões:** `Beat`, `Beat_2` e `Beat_3` da mesma pasta ficam numa linha só, com a mais nova em cima; a seta abre a pilha.
- **Combos:** listas com ordem (um show, um álbum, as faixas para um cliente). "Novo combo" na barra lateral; arraste uma
  linha da lista para o combo, ou use o botão direito → Adicionar ao combo. Na vista do combo, o botão direito numa linha dá
  a nota, Subir, Descer, "Seguir a versão mais nova" (o item toca sempre o `_N` mais novo) e Tirar do combo.
  O botão direito no combo, na barra lateral, toca em sequência (o item sem prévia é pulado), renomeia ou apaga (vai para a
  lixeira). Cada combo é um arquivo `combos\<nome>.wnlcombo` na pasta de dados; uma referência externa também pode entrar.
- **Importar de outro projeto:** botão direito numa linha → "Importar daqui...". Marque os padrões (cada um mostra os
  canais que tocam nele) e escolha: trazer as trilhas do mixer com os efeitos (desligado, só o volume e o pan) e levar as
  notas para o tom do projeto aberto. Os padrões e os canais entram com ids novos; o que já existe ganha "(de X)"; os
  samples apontam para o outro projeto até o próximo Ctrl+S, que os copia para a pasta deste. Ctrl+Z desfaz tudo de uma vez.
  A automação e a playlist do outro projeto não vêm.
- **Duplo clique** ou **Enter** abre o projeto (pergunta se salva o atual).
- A coluna Prévia diz: Pronta, Desatualizada (o projeto mudou depois dela), Sem prévia, Na fila, a porcentagem do render ou Falhou.

Pela IA ou pelo terminal: `library.list`, `library.refresh`, `library.renderPreview`, `library.renderPending`, `library.play`, `library.pause`, `library.stop`, `library.seek`, `library.deck`, `library.loadDeck`, `library.setActive`, `library.switchDeck`, `library.setSync`, `library.matchLoudness`, `library.loop`, `library.setMeta`, `library.search`, `library.toggleVersions`, `library.addFolder`, `library.removeFolder`, `library.combos`, `library.comboCreate`, `library.comboRename`, `library.comboDelete`, `library.comboAdd`, `library.comboRemove`, `library.comboMove`, `library.comboNote`, `library.comboLatest`, `library.comboPlay` e `project.importFrom` (este muda o projeto aberto e vira um passo de desfazer).

## 7e. Controlador MIDI e teclado do computador

- **Controlador**: em **F10** (Áudio e MIDI), seção "Controladores MIDI", ligue a entrada. O controlador toca o canal escolhido no channel rack (clique no nome do canal para escolher), como no FL. Conectou depois de abrir? Clique em "Procurar de novo".
- **Knobs e faders do controlador (MIDI learn)**: botão direito no fader ou no pan de uma trilha do mixer, num knob da janela de um efeito ou num macro do rack → **Aprender do controlador MIDI** → mexa o knob ou fader do controlador (em até 20 s; o pé da janela avisa). Pronto: ele move aquele controle, e uma mexida inteira é um passo de desfazer. Cada knob do controlador move um controle só, e cada controle tem um knob só: aprender de novo troca. **Esquecer o controlador** desfaz a ligação. As ligações ficam no projeto. Pela IA: `midi.learn`, `midi.map`, `midi.unmap` e `midi.mappings`.
- **Teclado-piano (Ctrl+T)**: a fileira Z S X D C V G B H N J M é uma oitava, e a vírgula dá o Dó seguinte. A de cima, Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P, vai do Dó central (Q, igual à vírgula) até o Mi 5 (P). Ligado, ele aparece aceso no pé da janela.
  - Os atalhos de uma tecla só (como o L do PAT/SONG) ficam desligados, como no FL. Espaço, as teclas F e os atalhos com Ctrl ou Alt continuam.
  - Num campo de texto (a busca, por exemplo) as letras escrevem, não tocam.

## 7f. Versões, autosave e recuperação

- **Salvar nova versão** (Ctrl+N no preset FL): grava `Beat_2.wnl` ao lado de `Beat.wnl`, depois `Beat_3.wnl`, e passa a trabalhar na versão nova. Projeto nunca salvo pergunta onde salvar.
- **Autosave**: a cada 2 minutos, se houve mudança desde o último salvar, vai uma cópia para `autosave\` dentro da pasta do projeto. Projeto nunca salvo vai para `%APPDATA%\WHYNOT LAB\Autosave`.
  - Ficam os 10 mais novos de cada projeto.
  - O autosave não marca o projeto como salvo e guarda os samples por caminho absoluto, então abre de qualquer pasta.
- **Recuperação**: se o app fechar sem ser pelo Sair (queda, falta de luz, processo morto), na abertura seguinte ele oferece abrir o último autosave. Aceitando, o Salvar grava no projeto de onde o autosave veio.

## 8. Atalhos (preset estilo FL)

Os atalhos são configuráveis em **Arquivo → Atalhos de teclado**. Há dois presets:
- **Estilo FL Studio** (padrão): as teclas que o FL usa para as mesmas ações.
- **WHYNOT**: o costume dos outros programas.

Além do preset, dá para trocar a tecla de qualquer ação: clique na ação e aperte a tecla nova. Uma tecla só dispara uma ação; se já estava em outra, sai de lá. As trocas ficam em `%APPDATA%\WHYNOT LAB\atalhos.json`.

As teclas do piano roll (P, B, E, S, Delete, Backspace, setas, Ctrl+A, Esc e Alt) são **fixas**: não aparecem no editor e valem nos dois presets. Na tabela abaixo elas estão marcadas como fixas.

Algumas ações não têm tecla no preset FL e ficam só no menu: **Novo projeto** e **Exportar stems**. **Atalhos de teclado** não tem tecla em nenhum dos dois presets; dá para pôr uma no editor.

| Tecla (preset FL) | Ação |
|---|---|
| Espaço | Tocar ou parar (volta ao início) |
| Ctrl+Espaço | Tocar ou pausar (continua de onde parou) |
| Ctrl+N | Salvar nova versão (`Beat_2.wnl`) |
| Ctrl+O | Abrir |
| Ctrl+S / Ctrl+Shift+S | Salvar / Salvar como |
| Ctrl+R / Ctrl+Shift+R / Ctrl+Shift+M | Exportar WAV / MP3 / MIDI |
| Ctrl+Z ou Ctrl+Alt+Z | Desfazer passo a passo (cada toque volta mais um) |
| Ctrl+Y ou Ctrl+Shift+Z | Refazer |
| Ctrl+T (ou o botão do teclado na barra de cima) | Teclado do computador como piano. Ligado: Z a M e Q a U tocam, - e = mudam a oitava, [ e ] a força |
| Ctrl+M | Metrônomo |
| L | Alternar PAT e SONG |
| F4 | Próximo padrão vazio depois do atual (cria um se não houver) |
| Ctrl+Shift+C | Clonar o padrão atual (passos e notas; o clone entra logo depois e fica escolhido) |
| + / - do teclado numérico | Próximo padrão / padrão anterior |
| F5 / F6 / F7 | Playlist / Channel rack / Piano roll (a tecla da tela aberta esconde ela, se o mixer estiver à vista) |
| F9 | Mostrar ou esconder o mixer |
| Alt+F8 | Navegador de sons |
| F8 | Navegador de plugins |
| P / B / E / C (fixas) | Piano roll: desenhar / pintar / selecionar / cortar a nota |
| P / B / C / D / E / T / S / Y (fixas) | Playlist: desenhar / pintar / cortar / apagar / selecionar / mudo / slip (o conteúdo anda dentro do bloco) / tocar daqui |
| Ctrl+arrastar (fixa) | Playlist e piano roll: caixa de seleção (Shift soma). Na régua da playlist: seleção de tempo, que a janela Exportar oferece como "Trecho"; duplo clique limpa |
| Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+B (fixas) | Playlist e piano roll: copiar / recortar / colar onde o mouse está / repetir a seleção logo à direita. No piano roll, o que foi copiado num canal cola em outro, com as mesmas durações e o mesmo espaço |
| Alt+Q (fixa) | Piano roll: quantizar os inícios na grade de agora (sem seleção, todas as notas) |
| Ctrl+L / Ctrl+G (fixas) | Piano roll: legato / juntar notas (sem seleção, todas) |
| Alt+M / Alt+Shift+M (fixas) | Piano roll: deixar as notas mudas / tirar o mudo |
| Alt+T | Marcador na agulha (na régua da playlist) |
| Shift+setas (fixa) | Playlist: mover os blocos selecionados pela grade ou de raia |
| A alça na régua (fixa) | Piano roll: com notas selecionadas, o triângulo no fim da seleção estica tudo no tempo |
| S (fixa) | Piano roll: slide nas notas selecionadas (sem seleção: liga ou desliga o modo Slide) |
| Delete ou Backspace, setas, Ctrl+A, Esc (fixas) | Piano roll: apagar, transpor e mover, selecionar tudo, limpar a seleção |
| Alt durante o arrasto (fixa) | Ignorar a grade |
| F10 | Áudio e MIDI |
| F11 | Projetos (ouvir a prévia sem abrir) |
| Tab | Projetos: trocar entre os decks A e B |
| Ctrl+= / Ctrl+- / Ctrl+0 | Aumentar / diminuir / 100% da interface |

No preset **WHYNOT** muda isto:

| Tecla | Ação |
|---|---|
| Ctrl+N | Novo projeto |
| Ctrl+Alt+N | Salvar nova versão |
| Ctrl+Alt+R | Exportar stems |

## 9. Áudio

**F10** (ou Arquivo → Áudio e MIDI) abre quatro coisas:
- a escolha de driver, aparelho, taxa de amostragem e buffer;
- a gravação: qual **entrada** da placa grava (uma em mono ou um par em estéreo) e o **ajuste da latência** em ms. Embaixo aparece quanto o take recua: entrada + saída da placa + motor + o seu ajuste. Se o take gravado sai atrasado em relação ao beat, aumente o ajuste; se sai adiantado, diminua. A entrada abre quando você arma uma trilha do mixer;
- os controladores MIDI (seção 7e);
- o teclado-piano.

Numa tela baixa, a janela rola em vez de passar da borda.

O padrão é **WASAPI** ("Windows Audio"). Com o ASIO SDK no build (seção 2b), o tipo **ASIO** aparece na lista. Os aparelhos ASIO aparecem quando há um driver ASIO instalado: o da sua placa de som, ou um genérico como o ASIO4ALL ou o FlexASIO. O botão do painel de controle do driver abre as opções dele. Buffer menor dá menos atraso e gasta mais CPU.

## 10. Autoteste e linha de comando

```
"build\whynot_lab_artefacts\Release\WHYNOT LAB.exe" --self-test build\selftest\relatorio.txt
```

Monta um beat por comandos, exporta, salva, reabre, exporta de novo e compara os dois WAVs amostra por amostra, além de desfazer/refazer, roteamento, mute/solo, volume do master e arquivos inválidos. Da 3.2: mede a frequência do 808 antes e depois do slide (65,4 → 98,0 Hz) e confere que ele não reinicia; mede o Bell (C5 = 523,3 Hz); testa mover, apagar e desfazer notas, padrão que cresce, passos que viram notas e tonalidade; exporta a música em modo song com um compasso vazio e um clipe de áudio; e confere que o beat completo com os faders padrão não clipa. No fim deixa um projeto de demonstração em `build\selftest\selftest_work\Demo 3.2\`. Da 3.4:
- exporta MP3 a 320 kbps e em VBR e decodifica de volta (tag ID3, 44,1 kHz, duração, pico e nível RMS iguais aos do WAV, tamanho de 320 kbps);
- exporta MIDI e relê (960 PPQ, notas do kick nos passos certos, 140 BPM);
- exporta stems e confere que a soma dá a mixagem, e que um bus de reverb sai só com o envio;
- grava versões `_2` e `_3`, faz autosave (com e sem mudança, guardando 10) e simula a recuperação depois de uma queda;
- testa os dois presets de atalhos, a troca de tecla, o arquivo `atalhos.json` e o Ctrl+Z que volta vários passos;
- confere que o ASIO aparece entre os drivers;
- testa a prévia do navegador chamando o callback de áudio à mão.

O código de saída é o número de falhas. Com o projeto como está no `main` (d335198), o relatório tem 205 linhas de ok.

O autoteste é dividido por área, um arquivo por área em `src/app/selftest/`. Cada frente acrescenta o seu arquivo. A ordem é esta:
- Core (3.1);
- Composition (3.2);
- Mixing: os efeitos nativos, o estilo trap e salvar e reabrir a mixagem;
- Plugins (3.3);
- Finishing (3.4);
- "Arquivos inválidos", sempre por último.

O `ctest --test-dir build` roda dois testes, sem abrir o app:
- `audio_core`: o núcleo de tempo real;
- `dsp`: os efeitos nativos da mixagem.

Para isso, compile antes os alvos `whynot_audio_tests` e `whynot_dsp_tests` (seção 3).

Outros modos:

```
"WHYNOT LAB.exe" --render "C:\caminho\Beat.wnl" saida.wav [repetições]
"WHYNOT LAB.exe" --screenshot tela.png ["C:\caminho\Beat.wnl"] [playlist|channelRack|pianoRoll|projects|sounds|plugins|mixer:<trilha>]
"WHYNOT LAB.exe" --screenshot-fx efeito.png "C:\caminho\Beat.wnl" <trilha> <slot 1-10>
```

- `--render` exporta sem abrir janela, seguindo o modo salvo no projeto (SONG = música inteira). Em PAT, o padrão repete 4 vezes se você não disser quantas. Se der errado, grava o motivo em `saida.erro.txt` e sai com código 1.
- `--screenshot` abre a janela, grava um PNG e fecha.
- `--screenshot-fx` grava um PNG da janela do efeito nativo que está no slot (de 1 a 10) da trilha.

A IA (Fase 4):

```
"WHYNOT LAB.exe" --control 47830 ["C:\caminho\Beat.wnl"]
"WHYNOT LAB.exe" --headless --control 0 [--control-file <json>] [--data <pasta>] [--parent-pid <pid>]
```

- `--control <porta>` abre a janela com a ponte da IA em 127.0.0.1. O aviso "IA conectada" aparece no pé, com "Desligar a IA".
- `--headless` abre sem janela, só a ponte. Quem usa é o servidor MCP (`mcp/`); ele abre e fecha o app sozinho.
- Como ligar uma IA, as tools e a segurança: `docs/mcp.md`.

A parte de plugins do autoteste usa os plugins de `build\test_plugins` (compile o alvo `whynot_test_plugins` antes). Ela confere o scan isolado (o plugin que derruba o processo e o que trava são bloqueados, com o motivo), o cache, o instrumento tocando as notas certas (440 Hz e 659 Hz), um parâmetro do plugin mudando o volume em 12 dB, o estado voltando do arquivo, o aviso de plugin ausente e o bloqueio depois de uma queda. O catálogo do autoteste fica numa pasta própria e não mexe no seu.

Validação dos plugins com o pluginval (Tracktion, GitHub oficial):

```
pluginval.exe --strictness-level 5 --validate "build\test_plugins\WHYNOT Teste Synth.vst3"
```

Os dois plugins de teste, o Surge XT 1.3.4 e o Surge XT Effects passam no nível 5.

## 11. Onde as coisas ficam

| O quê | Onde |
|---|---|
| Projetos (sugestão padrão) | `Músicas\WHYNOT LAB\` |
| Kit WHYNOT gerado | `%APPDATA%\WHYNOT LAB\Kit\` |
| Preferências da interface | `%APPDATA%\WHYNOT LAB\WHYNOT LAB.settings` |
| Configuração de áudio | gravada pelo Tracktion em `%APPDATA%\WHYNOT LAB\Settings.xml`; os arquivos temporários dele ficam em `%APPDATA%\WHYNOT LAB\Temporary\` |
| Plugins conhecidos, pastas e bloqueados | `%APPDATA%\WHYNOT LAB\plugins.xml` |
| Registro de "plugin abrindo" (recuperação após queda) | `%APPDATA%\WHYNOT LAB\carregando-plugin.txt`; só existe enquanto um plugin abre |
| Atalhos de teclado (preset e trocas) | `%APPDATA%\WHYNOT LAB\atalhos.json` |
| Cópia de um projeto antigo antes de converter para o esquema novo | `history\` dentro da pasta do projeto (uma por esquema) |
| Autosaves | `autosave\` dentro da pasta do projeto; os de projeto nunca salvo em `%APPDATA%\WHYNOT LAB\Autosave\` |
| Marca de sessão aberta (para achar autosave depois de uma queda) | `%APPDATA%\WHYNOT LAB\sessoes\sessao-<pid>.txt`; some ao fechar pelo Sair |
| Biblioteca de sons | `Músicas\WHYNOT LAB\Biblioteca\` |
| Samples do WHYNOT Piano (Salamander) | `%APPDATA%\WHYNOT LAB\Bibliotecas\Salamander\` (`whynot-piano.json`, `CREDITOS.txt` e `samples\`) |
| Pastas extras e abertas do navegador de sons | `%APPDATA%\WHYNOT LAB\Navegador.settings` |
| Ponte da IA: arquivo de controle (porta e token) | `%APPDATA%\WHYNOT LAB\control\<porta>.json`; só existe com o app aberto com `--control` |
| O que a IA fez | `%APPDATA%\WHYNOT LAB\control\ia.log` (e `ia.anterior.log`) |
| Prévias e capturas da IA | `%LOCALAPPDATA%\WHYNOT LAB\IA\`; o que tem mais de 24 h sai sozinho |

## 12. Problemas comuns

- **Sem som**: aperte F10 e escolha a saída certa; confira se o volume do master não está em -inf.
- **Apertei play e a playlist não tocou**: o transporte está em PAT. Clique em SONG (ou aperte L).
- **O slide não desliza**: o canal precisa estar em Mono (botão no piano roll ou menu do nome do canal).
- **O mixer sumiu ao abrir o piano roll**: em tela baixa ele sai para dar espaço; F9 traz de volta.
- **O plugin não aparece no navegador**: confira em Pastas se a pasta dele está na lista e clique em Escanear. Se estiver em Bloqueados, o motivo aparece ali.
- **Canal VST3 em vermelho**: o plugin falta neste PC ou foi bloqueado. A dica do nome do canal diz qual dos dois.
- **Build sem memória**: feche outros programas e use `-j 2`. O Docker Desktop ligado, com o WSL, ocupa uns 4 GB (o processo `vmmemWSL`): feche ele antes do build.
- **Exportar MP3 diz que o build não tem o LAME**: faltou a seção 2b (ou rodar o `cmake -S . -B build` de novo depois dela).
- **ASIO aparece mas sem aparelho**: não há driver ASIO instalado. A placa de som traz o dela; sem placa dedicada, use o WASAPI.
- **O controlador não toca**: ligue a entrada em F10 e clique no nome do canal que deve tocar. O controlador toca o canal escolhido.
- **As letras pararam de funcionar como atalho**: o teclado-piano está ligado (aceso no pé da janela). Ctrl+T desliga.
- **"git@github.com: Permission denied"** ao baixar o JUCE: rode o segundo comando da seção 2.
