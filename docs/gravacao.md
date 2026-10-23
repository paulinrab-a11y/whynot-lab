# Gravação, automação e desempenho na WHYNOT LAB

Gravar voz e notas tocadas ao vivo, automatizar parâmetros e manter a CPU sob controle (entrega 3.5). O modelo, os comandos e os testes estão prontos; as telas ficam com o Design (veja "O que falta na tela"). Tudo é da WHYNOT: nada copiado de DAW ou plugin comercial.

## Gravar

Como no FL:
- **Áudio:** arme a **trilha do mixer** da voz (a que tem a cadeia de efeitos dela).
- **MIDI:** arme o **canal** do instrumento.
- **REC:** a tecla **R** liga e desliga o REC. Com o REC ligado:
  - o **play grava**, e o **stop guarda o take**;
  - apertar R com a música tocando começa (ou termina) a gravação ali mesmo.

- **O assistente de placas e microfones (fatia 7; pedido do usuário de 21/10, 16:44):** na seção Gravação do Áudio e MIDI (F10):
  - **Microfone:** "Condensador com cabo XLR (precisa de 48V)", "Dinâmico com cabo XLR", "Microfone USB" ou "Não sei" (fica no `gravacao.json`).
  - **A placa** é reconhecida pelo nome que o driver informa, e é esse nome que aparece (nunca um nome de produto nosso): as interfaces USB mais comuns no Brasil, os microfones USB (que são a própria placa), o ASIO genérico e a placa do computador.
  - **Os passos:** o driver (o ASIO da placa; o Windows Audio para o microfone USB), o buffer de 128 a 256 em 48 kHz, o 48V para o condensador, mais ganho para o dinâmico, a entrada 1 em mono e o ganho até o pico ficar entre -18 e -10 dBFS. Com a placa devolvendo a voz e o app também, o aviso de eco (em vermelho).
  - **O medidor com alvo:** a faixa verde é o pico bom da voz; ele mede a entrada escolhida mesmo sem gravar, e diz "Baixo", "No alvo", "Alto" ou "Estourou". O medidor da trilha armada no mixer também mede antes do take.
  - **"Aplicar"** põe a gravação na entrada 1 em mono. O driver, o buffer e o 48V ficam com a pessoa.
- **Gravar voz em um clique (fatia 8; revisão de iniciante de 22/10):** o botão **Gravar voz**, na barra da playlist, acha a trilha da voz (a armada, a que se chama "Voz" ou uma nova "Voz"), arma, liga a contagem se estava desligada (com o último tamanho escolhido; 1 compasso no começo) e grava. Clique de novo para parar e guardar o take.
- **REC sem nada armado:** grava só as notas do canal escolhido (como no FL). A barra avisa na hora de ligar o REC, e o fim diz o que fazer ("clique em Gravar voz").
- **O microfone (aparelho), fatia 8:** na seção Gravação do Áudio e MIDI (F10), o primeiro campo escolhe de qual aparelho a voz vem. No Windows Audio a entrada é um aparelho à parte da saída: o microfone USB, a interface ou o microfone do computador ("O padrão do Windows" segue o que o Windows escolher). No ASIO, a entrada vem da própria placa. Fica no `gravacao.json`; com o aparelho desligado, grava pelo padrão do Windows e a barra avisa.
- **O beat em um gesto (fatia 6):** arraste o WAV ou o MP3 do beat do Explorer para a playlist. Num projeto vazio, o BPM vai para o do beat, no mesmo passo de desfazer: pelo nome do arquivo ("Trap 140bpm.wav", "beat_92_BPM", "BPM 130") ou, sem ele, pela análise (os ataques de até um minuto do beat, num pente de pulsos; fica entre 85 e 170 BPM). O bloco passa a cobrir o beat inteiro. Com mais coisa na playlist, a barra só diz o andamento do beat. Na setinha do bloco: "Acertar o BPM do projeto por este áudio", e o dobro e a metade (o dobro e a metade soam iguais para a conta).
- **De onde grava (gravar voz, 21/10):** no modo SONG, da **agulha**: o clique simples na régua da playlist a move, e o play, a gravação e o stop partem dela, como a marca de posição do FL. Com uma **seleção de tempo** (Ctrl+arrastar na régua), começa nela, e com o loop ligado a seleção vira o loop. No PAT, do começo do padrão.
- **Contagem (Ctrl+P):** 1 ou 2 compassos de clique antes do take. O botão direito no REC escolhe o tamanho, e o Ctrl+P liga e desliga com o último. O transporte começa antes da agulha (antes do zero, se for preciso), e a barra mostra "CONTAGEM" com os tempos que faltam. Com o metrônomo desligado, o clique toca só na contagem. A música antes da agulha também toca (pré-roll).
- **O take sempre se ouve:** o clipe de áudio só toca no SONG. Gravar áudio no PAT com algo na playlist passa para o SONG antes (um passo de desfazer à parte, com aviso). Com a playlist vazia, grava o padrão como ele toca, e ao guardar o padrão vai junto para a playlist, do zero até cobrir o take, e o modo passa para SONG (o mesmo passo de desfazer do take).
- **Passar do fim:** gravando no SONG sem seleção de tempo, a música passa do fim em vez de voltar ao começo, para o take seguir (como no FL). Com a seleção e o loop ligado, o take vai até a volta; o resto da passada fica no WAV.
- **Avisos** (passou para o SONG, nada armado, take curto) aparecem na barra de status.
- **Editar a voz (gravar voz, fatia 4):** com o mouse num bloco de áudio aparecem as alças:
  - **fades** nos cantos de cima: arraste para dentro (a reta do volume aparece por cima da onda);
  - **ganho** no meio da borda de baixo: arraste para cima ou para baixo (0,2 dB por pixel; com Shift, 0,05). O cabeçalho mostra o ganho ("+3,0 dB").
  - Na setinha do bloco: **Normalizar** (o pico do trecho em -1 dB, pelo ganho do bloco), **Inverter** (um WAV de trás para frente ao lado do original; inverter de novo volta para ele), **Consolidar** (os selecionados, ou o comp de um grupo de takes, num WAV só, com o ganho, os fades e os crossfades, sem os efeitos do mixer), **Ganho em 0 dB** e **Tirar os fades**.
  - O ganho e os fades não mexem no arquivo. Inverter e consolidar escrevem um WAV novo; o Ctrl+Z volta o bloco para o antigo, e o arquivo fica.
- **Take de áudio:** vira um **canal de áudio novo** no channel rack ("Voz take 1", "Voz take 2"...), que sai pela trilha armada, com **um clipe na playlist** (na raia que grava por essa trilha, senão na primeira livre). O arquivo vai para `gravacoes/` na pasta do projeto, em WAV 32 bits float, igual à entrada amostra por amostra, com o nome da trilha e o número: **"Voz_take03.wav"** (o primeiro número que os canais do projeto não usam; se o arquivo já existe, como o de um take desfeito, o novo ganha "(2)" e nada é sobrescrito).
- **Gravar pela playlist (fatia 5):** o botão direito no cabeçalho da raia escolhe a entrada da placa ("Entrada 1 (mono)", ou um par em estéreo) e **já arma**, como no FL. A raia ganha uma trilha do mixer com o nome dela (a voz passa pelos efeitos de lá), e os takes dela caem nessa raia. O cabeçalho mostra a entrada e o ponto vermelho quando armada. "Parar de gravar nesta raia" solta a raia e desarma.
- **Cancelar o take:** no botão direito do REC, gravando, "Cancelar o take" (ou o atalho, no editor de atalhos): o arquivo é apagado, nada entra no projeto e o transporte para.
- **Takes e comping (gravar voz, 21/10; desenho aprovado em `revisoes\desenho-takes-comping-2026-10-21.md`):**
  - **Em loop** (seleção de tempo e loop ligado), cada volta vira um take do mesmo grupo. A volta que mal começou (menos de um tempo) fica de fora. Um arquivo só: cada volta é um bloco com o corte dela.
  - **Gravar de novo no mesmo lugar** pela mesma trilha do mixer, sem loop, também entra no grupo de lá (o que já estava vira o take 1).
  - **O mais novo toca**, onde ele existe; os outros ficam mudos e recolhidos. A pílula "3 takes" no cabeçalho do bloco abre o grupo: uma sub-raia por take, embaixo da raia.
  - **Escolher:** um clique no pedaço apagado de um take faz ele tocar naquele trecho. Os outros takes calam ali.
  - **Cortar:** a ferramenta Cortar (C) em cima do grupo corta todos os takes no mesmo ponto.
  - **Crossfade:** onde um take encontra outro, 10 ms de crossfade automático, todo antes do corte (o que entra começa antes e sobe; o que sai desce). Onde o áudio continua (o mesmo take dos dois lados), nada muda.
  - **Mover:** arrastar um pedaço do comp leva o grupo inteiro. Apagar um pedaço apaga o trecho em todos os takes.
- **Take MIDI:** as notas viram **notas do padrão atual**, somadas às que já estavam. Com o loop ligado, a nota da segunda volta cai no lugar dela dentro do padrão. No modo SONG, a posição conta a partir do bloco do padrão na playlist.
- **De onde vêm as notas:** do teclado-piano (Ctrl+T), do controlador MIDI e do comando `live.note`. Sem canal armado, grava o canal ao vivo (o que o controlador toca).
- **Punch in/out:** com o punch ligado, só fica o que caiu entre os dois pontos. O arquivo guarda a passada inteira: o punch corta só o clipe, que dá para esticar depois.
- **Latência:** o take vai para trás pela latência da placa (entrada + saída), mais o atraso dos efeitos com lookahead, mais um **ajuste manual em ms** para quando o driver informa errado. As notas vão para trás só pela saída. O clipe começa num tick inteiro, e a fração vai para o corte do começo do arquivo: a posição fica exata na amostra.
- **Desfazer** (Ctrl+Z) tira o take inteiro: o canal, todos os blocos (as voltas de um loop também) e o que o grupo de takes mudou nos outros, ou as notas. O WAV fica no disco, e refazer traz de volta.
- **Entrada:** a primeira entrada da placa, em mono, por padrão. O comando `record.setInput` escolhe outra, ou um par em estéreo. Armar a trilha já abre a entrada da placa, que reinicia uma vez.
- **Monitoração pelo app (gravar voz, 21/10):** a voz toca pela trilha do mixer, com os efeitos dela. O botão direito no armar (●) escolhe:
  - **Quando armada** (o padrão): a trilha armada toca a entrada, **só de fone** (fatia 8). Na primeira vez que uma trilha arma, o app pergunta "Está de fone?". Nas caixas, ou com a placa do computador, a escuta fica desligada e a barra explica: a voz voltaria pelo alto-falante e viraria microfonia. "Estou de fone", no mesmo menu, muda a resposta;
  - **Desligada**: só a monitoração direta da placa;
  - **Sempre**: toca a entrada mesmo desarmada.
  - Fora do padrão, a placa do nome da trilha ganha um ícone.
  - Na primeira vez, a barra avisa do eco: se a placa também devolve a voz, deixe uma só das duas.
- **O take é sempre seco:** o gravador pega a entrada antes de tudo, então os efeitos da trilha só valem no que se ouve.
- **Baixa latência ao gravar** (no mesmo menu, desligada por padrão): gravando com a monitoração, os efeitos com atraso (lookahead) da trilha que monitora e do master saem do que se ouve. Voltam ao parar. O motor tira os plugins desligados do grafo só enquanto dura, e o atraso deles some.

## Automação

Uma **curva por parâmetro**, em ticks da música, como os clipes de automação do FL:
- move o volume e o pan de uma trilha (vale o master);
- move qualquer parâmetro visível de um efeito nativo, como o corte do filtro;
- move os parâmetros de um efeito ou instrumento VST3.

- **Só toca no modo SONG.** No PAT, o parâmetro fica no valor do knob. Não é bug: é como no FL, e a raia na playlist deve dizer isso na dica.
- **Entre dois pontos a curva anda reta na posição do knob**, não no valor: numa frequência, de 200 Hz a 8 kHz o meio do caminho é 1,3 kHz, como um knob girado com o mouse. O volume anda reto no fader. Antes do primeiro ponto vale o primeiro; depois do último, o último.
- **Mudar o BPM leva a curva junto** (ela está em ticks, como as notas e os clipes).
- **Quando a curva sai** (apagada, sem pontos, ou o modo volta para PAT), o volume, o pan e os efeitos nativos voltam ao valor do mixer ou do efeito. Um VST3 fica onde a curva o deixou, como no FL.
- **Apagar a trilha ou o efeito apaga a curva dele** (com aviso no log). O desfazer devolve os dois.
- **Ainda não tem:** curvas com tensão e gravar automação mexendo no knob durante o play.

## Medidor de CPU e falhas de buffer

- **CPU:** a carga da thread de áudio. Sobe na hora, desce suavizada, e o pico fica guardado por 2 s.
- **Falhas de buffer** (os estalos quando a CPU não dá conta): o que a placa conta mais o que o JUCE detecta. Contam só as novas desde que o app abriu, ou desde zerar.
- **Aviso:** acende com a CPU em 85% ou mais, ou com uma falha nos últimos 3 s. Hora de congelar alguma coisa ou subir o buffer em Áudio e MIDI (F10).

## Congelar

Congelar transforma o som de uma **trilha do mixer** num WAV. Os instrumentos dos canais que entram nela e os efeitos antes do fader **param de gastar CPU**; o fader, o pan e os envios continuam vivos.

- **"Congelar canal" congela a trilha do mixer por onde o canal sai.** Todo canal novo ganha a própria trilha, então na prática é o canal. Se a trilha recebe vários canais, congelar um **congela todos**, e o app diz quais.
- **Quando não dá:** um canal que sai direto no master não congela (o motor só congela trilha que sai para a placa). Uma trilha que sai para um bus também não; congele o bus.
- **O conteúdo mudou** (notas, sample, efeitos, BPM, PAT/SONG): a trilha congela de novo sozinha. Isso renderiza na hora, então editar um canal congelado é lento. Descongele para trabalhar nele.
- **Mexeu dentro de um VST3?** O estado dele só entra no projeto ao salvar: descongele e congele de novo.
- **O export não usa o congelado:** sai sempre o som vivo, igual com ou sem congelar.
- Congelar é um passo de desfazer, como armar.

## O que falta na tela (Design)

O modelo e os comandos estão prontos para:
- **Gravar:** o botão de armar (bolinha) na trilha do mixer e no canal, e o REC aceso na barra de transporte (piscando enquanto grava). Também o medidor de entrada na trilha armada e a faixa do punch na régua da playlist. Em Áudio e MIDI (F10), o ajuste de latência e a escolha da entrada.
- **Automação:** a raia na playlist, com a dica "toca no modo SONG".
- **Desempenho:** o medidor de CPU na barra de transporte (laranja com o aviso) e o contador de falhas que zera com um clique.
- **Congelar:** um floco de neve na trilha e no canal, e os efeitos da trilha congelada apagados.

Em C++, pela `AppController`:
- **`recorder()`:** `isRecordMode()`, `isRecording()`, `takeInputPeak()`, `input().writtenSeconds()` e `lastMessage()`.
- **`performanceSnapshot()`:** o medidor, atualizado a cada 100 ms.
- **`getProjector()`:** `isFrozen (trilha)` e `freezeProblems()`.
- **Para desenhar a curva igual ao som:** `automation::valueAt` e `Range`, em `src/automation/AutomationMath.h`.

## Para a IA (comandos)

- Fatia 8: `record.voice` (acha ou cria a trilha "Voz", arma, liga a contagem e o REC; o play vem depois, com `transport.play`), `record.inputDevices` e `record.setInputDevice { name }` (o microfone no Windows Audio; "" = o padrão), `record.setHeadphones { answer: yes | no | "" }` (a escuta "Quando armada" só vale com yes). O `record.state` traz `inputDevice` e `headphones`.

| Comando | O que faz |
|---|---|
| `record.arm {track ou channel, on?}` | Arma a trilha do mixer (áudio; uma por vez, o master não) ou o canal (MIDI) |
| `transport.record {on?}` | O REC (a tecla R): parado, o próximo play grava; tocando, grava a partir de agora ou termina e guarda |
| `record.setPunch {on?, start?, end?, unit?}` | Punch in/out (com start/end liga) |
| `record.setLatency {adjustMs}` | Ajuste manual da compensação (-500 a 500 ms; positivo = o take vai mais para trás) |
| `record.setInput {first, stereo?}` | Qual entrada grava (1 = a primeira) |
| `record.setMonitor {track, mode}` | Monitorar a entrada pela trilha: `off`, `armed` (o padrão) ou `on` |
| `record.setLowLatency {on}` | Baixa latência ao gravar: os efeitos com atraso saem da escuta enquanto grava (fica no `gravacao.json`) |
| `audio.setGain {item, db}` | O ganho do bloco de áudio, de -60 a +24 dB |
| `audio.setFades {item, in?, out?}` | Os fades do bloco, em segundos |
| `audio.normalize {item, peakDb?}` | Normaliza o trecho do bloco (o pico em -1 dB, ou no `peakDb`) pelo ganho do bloco |
| `audio.reverse {item}` | O bloco passa a tocar o mesmo trecho de trás para frente (um WAV novo ao lado; de novo, volta) |
| `audio.consolidate {items}` | Os blocos viram um WAV só (sem os efeitos do mixer), num bloco no lugar deles |
| `takes.list {group?}` | Os grupos de takes: os takes e, em cada trecho, qual toca |
| `takes.cut {group, at, unit?}` | Corta todos os takes do grupo no mesmo ponto |
| `takes.choose {item}` | Escolhe o take daquele pedaço: ele toca, e os outros calam onde ele está |
| `audio.detectBpm {item}` | O andamento do áudio: pelo nome do arquivo ou pela análise, com o dobro e a metade |
| `audio.matchBpm {item, bpm?, auto?}` | Põe o projeto no BPM do áudio (ou no `bpm`), e o bloco cobre o áudio inteiro; `auto` só se a playlist não tiver mais nada |
| `record.guide {driver?, device?}` | O assistente: a placa reconhecida, o microfone e os passos (driver e device simulam outra placa) |
| `record.setMic {type}` | O tipo de microfone: `condenser`, `dynamic`, `usb` ou vazio |
| `record.cancel` | Cancela o take em andamento: apaga o arquivo e para |
| `playlist.setLaneInput {track, first, stereo?}` | Gravar na raia: a entrada da placa (1 = a primeira; 0 = para de gravar), cria a trilha do mixer dela e arma |
| `record.setCountIn {bars?}` | Contagem antes de gravar: 0, 1 ou 2 compassos; sem `bars`, liga e desliga com o último tamanho (Ctrl+P) |
| `transport.setPosition {at, unit?}` | Move a agulha: no SONG, o play e a gravação começam dela, e o stop volta para ela |
| `record.state` | REC, gravando, armados, punch, latências em ms, entrada, notas do controlador perdidas, aviso da última gravação |
| `record.note {key, on?, velocity?, channel?, at?}` | Põe uma nota no take com o tempo dela (segundos no relógio da placa); não toca o som |
| `record.commitTake {audio?, midi?}` | Guarda um take (o app chama sozinho no stop; um passo de desfazer "Gravação") |
| `automation.addLane {target}` | Cria a curva de um parâmetro (`target`: `{kind: mixer, track, param: volume ou pan}`, `{kind: insert, track, insert, param}` ou `{kind: channel, channel, param}`); se já existe, devolve a mesma |
| `automation.setPoints {lane, points, unit?}` | Troca os pontos: `[{t, v}]` com v de 0 a 1 (a posição do knob), ou `[{t, value}]` na unidade do parâmetro |
| `automation.removeLane {lane}` | Apaga a curva |
| `automation.list`, `automation.targets {track ou channel}` | As curvas (com o valor em unidade), e o que dá para automatizar (faixa, unidade, escala) |
| `audio.performance`, `audio.resetXruns` | CPU, pico, falhas, aviso, trilhas congeladas; zera as falhas |
| `channel.setFrozen {channel, on?}`, `mixer.setFrozen {track, on?}` | Congela ou descongela; a resposta lista os canais que congelaram juntos |

`unit` vale para os tempos: ticks (960 por tempo, padrão), steps, beats ou bars. A linha do tempo vai até 10.000 compassos.

## No arquivo `.wnl`

O esquema **continua 3**: tudo é campo opcional, que só sai no arquivo quando diferente do padrão. Um projeto que não grava nem automatiza sai igual.

- Canal e trilha do mixer: `"armed": true`. Trilha do mixer: `"frozen": true` e `"monitor": "off"` ou `"on"` (sem o campo, quando armada).
- Projeto: `"punch": { on, in, out }`.
- Raia da playlist: `"recordTrack"`, a trilha do mixer que grava nela (a entrada escolhida no cabeçalho).
- Bloco da playlist: `"takeGroup"` e `"take"` (o grupo de takes e o número); o que toca é o que não está mudo. No áudio, `"gainDb"`, `"fadeIn"` e `"fadeOut"` (segundos), quando não são zero.
- `"automation": [{ id, target: { kind, track, channel, insert, param }, points: [{ t, v }] }]`, com `v` de 0 a 1 na faixa do parâmetro.
- O `normalise` cuida do que vem estragado:
  - limita `t` à linha do tempo e `v` a [0, 1];
  - ordena os pontos e deixa um por `t` (o último vence);
  - tira a curva de um alvo que não existe mais;
  - deixa uma curva por parâmetro e uma trilha armada só.
- Os takes são WAVs como qualquer sample: no primeiro salvar de um projeto sem nome, eles são copiados para `samples/`.

## Como funciona por dentro

- **O tempo da gravação:** passa pelo relógio da placa (as amostras desde que ela abriu, o mesmo que o Tracktion usa para tocar). O ponto em comum entre esse relógio e a linha do tempo é medido quando a agulha já anda, porque o play do Tracktion é assíncrono. As contas estão em `src/record/TakeMath.h`.
- **Tempo real:** a thread de áudio (`InputRecorder::pushInput`) só copia a entrada para pedaços fixos numa fila sem trava, sem alocar, travar ou tocar no disco. Uma thread de fundo escreve o WAV.
  - Se a placa falha, ou o disco atrasa e a fila enche (por qualquer tempo), o buraco vira silêncio e o resto do take continua no tempo certo.
  - Se a placa reinicia no meio (troca de taxa ou de buffer), o take para ali, com aviso, em vez de seguir adiantado.
  - A posição de cada bloco não depende da ordem dos callbacks da placa: o `BlockClock` descobre a ordem quando a placa começa.
  - Os controladores MIDI entram por um `Consumer` do Tracktion, com uma fila por aparelho. Nota que não coube na fila é contada (`midiDropped` no `record.state`) e avisada.
- **Automação:** o `Projector` põe cada curva no `AutomatableParameter` do Tracktion, em segundos pelo mapa de tempo. Ele chama `updateStream()` na hora, porque o temporizador do Tracktion não roda no export sem janela.
  - Nos efeitos nativos, cada parâmetro visível é automatizável. Os pontos de 2 a 5 dessa parte foram combinados com a Mixagem, dona do arquivo: a curva passa pelo clamp do parâmetro, o valor do Insert volta quando ela sai e o modelo manda.
- **Congelar:** `AudioTrack::setFrozen` (individual) só no Edit ao vivo. Enquanto congelada, os plugins dos canais que entram nela ficam desligados.
- **Revisões de tempo real e numérica** (skills `audio-dsp-review` e `audio-numerics-review`): estão nas mensagens dos commits do `InputRecorder` e do `WhynotFx`.

## Testes

- **Sem JUCE** (ctest):
  - `record`: o relógio, a latência, onde o take cai, o punch, as notas, a posição do bloco nas duas ordens de callback e os buracos; o começo da gravação (a agulha, a seleção, a contagem), as voltas do loop e o andamento do beat (o nome e beats sintéticos de 87,5 a 140 BPM);
  - `automation`: a posição do knob, os pontos e o valor da curva;
  - `perf`: o medidor.
- **`--self-test`, seção "Gravação":** a entrada são blocos de um WAV conhecido empurrados no gravador, como a placa faria.
  - o take é igual à entrada (diferença máxima 0);
  - o clique toca exatamente 480 amostras antes, com a latência simulada de 128 + 352;
  - o desfazer e o refazer;
  - o segundo take no mesmo lugar, com o ajuste de 2,5 ms: vira o take 2 do grupo e toca, e o primeiro fica mudo, igual;
  - o punch cortando na amostra do tick certo;
  - as notas nos ticks certos (com a volta do loop e a nota que já estava);
  - o salvar e reabrir, a falha de buffer virando silêncio e a placa que reinicia no meio (o take para ali).
- **Seção "Gravar voz"** (`selftest/Voice.cpp`), com a mesma placa simulada:
  - o Ctrl+P e o liga e desliga da contagem;
  - o REC no PAT com a playlist cheia passa para o SONG; com 1 compasso de contagem, o transporte começa 1 compasso e meio tempo antes da agulha, o clipe começa nela e o clique cantado toca na amostra certa no export;
  - a seleção de tempo vira o loop, o take fica dentro dela, e o stop volta para o começo dela;
  - no PAT com a playlist vazia, a contagem de 2 compassos começa antes do zero com o loop desligado, o padrão vai junto do take e o modo passa para SONG; o Ctrl+Z desfaz tudo;
  - as capturas `voz-contagem.png` (a barra na contagem) e `voz-agulha.png` (a agulha na régua);
  - a monitoração: "Quando armada" por padrão, "Desligada", "Sempre" desarmada, o desfazer, o `.wnl` reaberto e a captura `voz-mixer.png`;
  - o take seco: com a trilha monitorando e uma saturação nela, o take é igual à entrada;
  - takes e comping: 3 voltas e meia em tons diferentes viram 4 takes; o mais novo toca (medido em Hz no export); o crossfade no corte não deixa degrau; cortar todos, escolher o take 1 num pedaço e ouvir o comp no export; o Ctrl+Z da escolha; a passada sem loop virando o take 5; o `.wnl` reaberto soando igual; a captura `voz-takes.png` com o grupo aberto;
  - a edição do clipe: consolidar o comp soa igual (diferença máxima medida) e o Ctrl+Z volta os pedaços; +6 dB medidos no export; os fades de 0,4 s medidos no começo, no meio e no fim; normalizar põe o pico em -1 dBFS; inverter põe o clique de 0,5 s em 1,5 s, e inverter de novo volta; as alças com o mouse (o fade e +6 dB com 30 px); o `.wnl` reaberto; a captura `voz-clipe.png`;
  - os arquivos e a entrada pela playlist: a entrada no cabeçalho cria a trilha e arma; o take cai na raia, como "Track_2_take01.wav" e depois "_take02"; cancelar apaga o arquivo e não põe nada no projeto; um Ctrl+Z tira o take em loop inteiro e devolve os outros takes como estavam; o `.wnl` reaberto; a captura `voz-raia.png`;
  - o beat em um gesto: soltar um beat a 92 BPM sem o andamento no nome põe o projeto em 92 e o bloco cobre o beat; o Ctrl+Z tira os dois; com mais coisa na playlist, a barra só diz o andamento; "Trap 150bpm" acerta pelo nome, e a metade dá 75; a captura `voz-beat.png`;
  - o assistente: a interface com o condensador (48V, ASIO, mono, o aviso de eco), o microfone USB, a placa do computador e a outra interface; o medidor lendo a entrada escolhida sem gravar (-14 dBFS, no alvo); a janela Áudio e MIDI com o microfone e os passos; a captura `voz-assistente.png`.
- **Seção "Automação":** ruído branco num filtro passa-baixas.
  - A curva de 200 Hz a 8 kHz faz o brilho subir janela a janela: a energia acima de 2 kHz no compasso 4 fica mais de 20 dB acima da do compasso 1.
  - O meio bate com o corte pedido em escala log.
  - Também: o desfazer, a volta ao valor do efeito, o volume de -30 a 0 dB, o salvar e reabrir e o efeito removido.
- **Seção "Desempenho":**
  - o medidor sem placa;
  - o som congelado do Kick bate com o render sem congelar, por diferença máxima, com um EQ na trilha;
  - o sampler do Kick desligado enquanto congelado;
  - congelar de novo quando o padrão muda, e o export igual;
  - o salvar e reabrir, o descongelar e o desfazer;
  - dois canais na mesma trilha, listados;
  - as recusas.

## O que não foi testado de verdade

- **Fatia 8 sem placa:** a lista de microfones do Windows Audio, a troca do aparelho com a entrada aberta e a pergunta "Está de fone?" só aparecem com o app aberto; o autoteste usa nomes simulados e confere o número da pergunta, não a janela.

- **O assistente com a placa de verdade:** o reconhecimento foi testado com nomes simulados. Os nomes que cada driver informa de verdade podem variar; o usuário confere com a dele e o AT2020.
- **Sem microfone:** a entrada do autoteste é simulada. Gravar pela placa de verdade (WASAPI e ASIO) e abrir a entrada não rodaram.
- **Monitoração pelo app:** ligar a entrada da placa na trilha (as entradas do Tracktion, mono ou em par), o atraso no fone, o eco e a baixa latência só aparecem com a placa. No autoteste não há placa.
- **Latência real:** os números que a placa informa não foram conferidos. Na segunda, um teste de ida e volta (cabo da saída na entrada, gravar um clique) mostra se precisa do `adjustMs`.
- **Sem controlador MIDI:** as notas do teclado de hardware não foram gravadas de verdade; o caminho do `live.note` e do teclado-piano é o mesmo do teste.
- **Medidor:** sem placa, a CPU e as falhas ficam em 0 no autoteste; os números reais só aparecem com o app aberto.
- **Ouvido:** nada disso foi ouvido. Os WAVs do autoteste ficam em `selftest_work/gravacao`, `selftest_work/automacao` e `selftest_work/desempenho`.
