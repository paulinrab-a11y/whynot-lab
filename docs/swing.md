# Swing, groove e começo do play no PAT

Pedidos dos produtores de 29/10:

- "Swing tira as coisas do tempo, pra ficar mais dinâmico e natural."
- Escolher de onde o beat começa a tocar no modo PAT e no piano roll, como já dá na playlist.

Frente `extra-swing-inicio`.

## 1. Swing global (o knob no topo do channel rack)

- **Onde:** o knob "Swing" no cabeçalho do rack, entre o título e os números dos tempos. Vai de 0 a 100%.
  - Duplo clique zera.
  - Um arrasto vira um passo de desfazer ("Swing").
- **Comando:** `project.setSwing { amount }`, com desfazer.
- **Arquivo:** campo `"swing"` no objeto `project` do `.wnl`, opcional e só quando é diferente de 0. O esquema não sobe.

### Como o global se junta ao do canal

O swing global **soma** ao swing do canal, até 100%.

- O swing do canal continua no botão direito do nome do canal (`channel.setSwing`).
- Conta: `Project::stepSwingTicks (canal) = swingTicksFor (min (100, global + canal))`.
- 100% atrasa o passo do contratempo em 1/3 de passo (80 ticks), a sensação de tercina. É a mesma escala de antes.

Por que soma, e não uma fração do global:

- Um projeto antigo com swing por canal toca igual, porque o global começa em 0.
- O botão do rack faz o que a pessoa espera, que é "mais swing em tudo".
- O canal que já tinha swing continua com mais que os outros.

Quem precisa de um canal reto com o global ligado tem dois caminhos: tirar o canal do padrão ou usar o groove do piano roll.

### O que o swing global afeta

- Vale para os **passos** do step sequencer, como no swing do canal.
- Não vale para as notas do piano roll: elas têm o próprio groove (parte 2).
- Passar os passos para o piano roll (`pianoroll.stepsToNotes`) grava as notas onde os passos soavam, já com o global.

## 2. Groove das notas (o botão "Groove" na barra do piano roll)

O botão fica depois do Glide. É um menu, como o do glide, com duas partes:

- **Swing das notas (só ao tocar):** 0, 25, 50, 66, 75 e 100%.
  - As notas que caem no contratempo dos 16 avos atrasam como os passos com swing.
  - As outras posições andam em linha reta dentro da colcheia (`Projector::swingWarp`): o começo e o fim da colcheia ficam, e o meio atrasa. Notas de tercina e de 32 avos também ganham o balanço, sem trocar de ordem.
  - O fim da nota passa pela mesma conta, e a duração acompanha.
- **Humanizar ao tocar (as notas não mudam):** Desligado, Leve (15%), Médio (35%) e Forte (60%).
  - Cada nota e cada passo do canal ganha um pouco de tempo (até ±40 ticks em 100%) e de força (até ±20) ao acaso.
  - É a mesma escala do Editar > Humanizar, que muda as notas de verdade.

Detalhes do botão e do comando:

- O botão acende quando o canal tem swing ou humanizar, e com swing mostra "Swing 50%".
- **Comando:** `channel.setGroove { channel, swing?, humanize? }`, com desfazer. O canal de áudio recusa.
- **Arquivo:** `"noteSwing"` e `"humanize"` no canal, opcionais. O esquema não sobe.

### Só na hora de tocar

- Tudo acontece no `Projector::notesFor`, a mesma conta do play, do export WAV/MP3 e do MIDI exportado.
- As notas no modelo continuam onde a pessoa pôs, e a quantização (Alt+Q) continua na grade.
- Desligar o groove volta ao som de antes, sem desfazer nada.

### O sorteio do humanizar é fixo

- O splitmix64 roda sobre os números da semente, do canal, da nota e da volta, sem estado. É a mesma ideia da chance dos passos.
- A nota entra pelo passo (e a repetição do roll) ou pelo id da nota no piano roll. Mexer numa nota não sorteia as outras de novo.
- Dois renders saem iguais amostra por amostra.
- Cada volta do padrão sorteia de novo. Por isso, no PAT com loop, o padrão toca 8 voltas antes de repetir, como com a chance (`Projector::humanizes`).

## 3. De onde o play começa no PAT

- **Na régua do piano roll:** um clique marca de onde o padrão atual começa a tocar.
  - O clique segue a grade. Com Alt, fica sem grade, como na régua da playlist.
  - A marca é o triângulo da agulha da playlist, branco quando não está no começo. Parado, uma linha fina mostra o ponto na grade.
- **No cabeçalho dos passos do rack:** um clique sobre a faixa dos números dos tempos marca o passo.
  - O triângulo aparece sobre o passo.
  - Com o mouse em cima, um traço mostra o passo.
- **Atalho:** Alt+← e Alt+→, com o rack ou o piano roll na tela, andam o ponto um tempo para trás ou para a frente. Na playlist, o Alt+setas pula a agulha de marcador em marcador.
- **Stop:** volta para o ponto, como a agulha da playlist.
- **Loop:** continua dando a volta no padrão inteiro. Começa no ponto e, no fim, volta para o compasso 1.
- **Comando:** `transport.setPatternStart { at, unit }`. É estado do transporte, como o `transport.setPosition`: não entra no desfazer nem no `.wnl`.
- **Vale por padrão:**
  - Trocar de padrão volta ao começo. Voltar ao padrão traz o ponto dele de novo.
  - Se o padrão encolher e o ponto ficar fora, ele vale 0.
- **Gravando no PAT:** o take começa do começo do padrão, como antes, porque o padrão vai inteiro para a playlist.
- **No SONG:** quem manda é a agulha da playlist. O clique na régua do piano roll e na faixa do rack só avisa no pé.

## Testes

- Autoteste, seção "Swing e começo do play no PAT (29/10)" (`src/app/selftest/Groove.cpp`):
  - o render com swing global de 50% atrasa o contratempo 857 amostras (40 ticks a 140 BPM e 48 kHz);
  - a soma com o canal;
  - o desfazer;
  - o swing das notas no render e no modelo;
  - os limites e a repetição do humanizar;
  - o `.wnl`;
  - o play e o stop no ponto, o loop, a troca de padrão e o SONG;
  - o knob, os cliques na faixa e na régua, o menu Groove e as capturas.
- `tests/control/test_control_core.cpp`: 264 comandos classificados. Os 3 novos são livres.
