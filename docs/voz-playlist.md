# Editar a voz na playlist

Frente A de 27/10. O pedido do usuário: deixar a edição do áudio gravado pelo microfone mais fácil, mais perto do jeito de editar na playlist do FL Studio. Estudado só por fora (manual e vídeos), sem copiar nada.

## O que muda para quem edita

- **O corte não afunda mais o som.** Antes, cortar um clipe com fades copiava os dois fades para os dois pedaços, e no ponto do corte o volume caía a quase zero (medido: de 0,125 para 0,003, uns -32 dB). Agora o pedaço da esquerda fica com o fade de entrada, o da direita com o de saída, e no corte o som continua amostra por amostra. Vale para a ferramenta Cortar (C), para o Picotar e para o corte dos takes.
- **Cortar na agulha: Ctrl+E.** Corta tudo o que passa pela agulha (tocando, onde o play está). Com uma seleção de tempo na régua (Ctrl+arrastar), corta nas duas bordas dela. Com blocos selecionados, só neles. Um passo de desfazer.
- **Apagar o trecho e puxar o resto: Ctrl+Delete.** Com uma seleção de tempo, o trecho sai da música inteira e o que vem depois anda para trás (os blocos, os marcadores e os pontos de automação), como cortar um pedaço da fita.
- **Abrir espaço: Ctrl+Insert.** Abre um espaço vazio do tamanho da seleção, no começo dela, e empurra o resto para a frente.
  - Os três também estão no botão direito da régua.
- **A curva do fade.** Arraste a alça do canto de cima para o lado (o tamanho do fade) e para cima ou para baixo (a curva): para cima fica **rápida** (sobe logo, a de mesma potência), para baixo fica **lenta**. Na setinha do bloco, "Curva do fade de entrada" e "de saída" escolhem também a **reta** e a **em S**. A linha na tela segue a curva que toca.
- **Crossfade automático.** Dois áudios que se sobrepõem na mesma raia fazem um crossfade na sobra: o que acaba desce e o que começa sobe, com a curva de mesma potência, sem somar dobrado. Na tela, a curva do crossfade aparece em laranja. Um bloco inteiro dentro de outro é camada, não crossfade.
- **Tom do bloco.** Na setinha: "Tom do bloco" (de -12 a +12 semitons no menu; o comando aceita de -24 a +24, com centésimos). Transpõe só aquele pedaço, sem mudar o tempo e **sem canal novo**. O cabeçalho mostra "+2 st".
- **Inverter sem canal novo.** "Inverter" agora vira só o bloco: o arquivo invertido fica ao lado do original ("Voz_take03_invertido.wav"), e o bloco continua no canal dele. O cabeçalho mostra "inv", e a onda aparece espelhada. Se o arquivo invertido sumir (o projeto foi copiado sem ele), o app escreve de novo a partir do original.
- **Normalizar o grupo.** Com vários áudios selecionados, "Normalizar os N selecionados juntos" dá o mesmo ganho a todos: o pico mais alto vai a -1 dB e o equilíbrio entre eles fica.
- **Tirar silêncio e respiração.** Na setinha, "Tirar silêncio e respiração...":
  - o **limiar** (de -70 a -20 dB) e o **silêncio mínimo** (de 100 a 1000 ms) mudam a prévia na hora: os trechos que saem ficam em vermelho no bloco;
  - uns -45 dB tiram só o silêncio; perto de -30 dB, a respiração também;
  - **Tirar** corta o clipe e apaga os trechos quietos (com 5 ms de anti-clique nas bordas que encostam no buraco); ou, com "Abaixar 15 dB em vez de apagar", deixa a respiração mais discreta;
  - um passo de desfazer.

## Para a IA (comandos)

| Comando | O que faz |
|---|---|
| `playlist.cutAt {at, unit?, items?}` | Corta tudo o que passa por `at` (ou só os `items`); devolve os pedaços da direita |
| `playlist.removeSpace {from, to, unit?}` | Apaga o trecho da música inteira e puxa o resto (blocos, marcadores, automação) |
| `playlist.insertSpace {at, length, unit?}` | Abre um espaço vazio e empurra o resto |
| `audio.setFades {item, in?, out?, inCurve?, outCurve?}` | Fades do bloco; a curva é `linear`, `convex` (rápida), `concave` (lenta) ou `s` |
| `audio.setPitch {item, semitones}` | O tom do bloco, de -24 a +24 semitons, sem mudar o tempo |
| `audio.reverse {item}` | Vira o bloco (de novo = volta), sem canal novo |
| `audio.normalize {item ou items, together?, peakDb?}` | Normaliza um bloco ou um grupo (`together` true = o mesmo ganho) |
| `audio.findSilence {item, thresholdDb?, minMs?, padMs?}` | A prévia: os trechos quietos, em ticks; não mexe no projeto |
| `audio.removeSilence {item, thresholdDb?, minMs?, padMs?, reduceDb?}` | Tira (ou, com `reduceDb`, abaixa) os trechos quietos |

## No arquivo `.wnl`

O esquema não muda: são campos opcionais do bloco de áudio, que só saem quando diferentes do padrão, iguais no `toJson` e no `toJsonReference`:

- `"fadeInCurve"` e `"fadeOutCurve"`: `"convex"`, `"concave"` ou `"s"` (sem o campo, a reta);
- `"pitch"`: semitons, com duas casas;
- `"reversed": true`: o bloco toca o arquivo invertido ao lado do original, e o `offsetSeconds` conta nele.

## Como funciona por dentro

- As contas ficam em `src/model/PlaylistEdit.h`, só no modelo: o corte (`splitAt`), cortar num ponto (`cutAt`), apagar e abrir espaço, o crossfade automático e a curva de ganho da tela.
- O `Projector` passa a curva para o fade do Tracktion (reta, convexa, côncava e em S). O crossfade automático usa a convexa, a de mesma potência, como o crossfade dos takes. Vale o fade mais longo entre o do bloco, o dos takes e o automático.
- O tom do bloco liga o esticador signalsmith (MIT, no Tracktion) com o `setPitchChange`. Na fita, o tom do bloco soma ao que a velocidade dá.
- O arquivo invertido: `src/record/ReversedAudio.h`. O normalizar, o picotar pelas batidas, o groove e o silêncio leem o arquivo que o bloco toca.

## Testes

No autoteste, seção "Voz na playlist" (`src/app/selftest/VoicePlaylist.cpp`):

- o corte medido no WAV contra o clipe inteiro (Cortar, Picotar a cada tempo e o corte dos takes);
- `cutAt`, `removeSpace` e `insertSpace` no modelo e no som (uma voz que cresce mostra de que segundo do arquivo cada pedaço veio), os atalhos Ctrl+E, Ctrl+Delete e Ctrl+Insert na grade sem janela, e o desfazer;
- cada curva medida no WAV a 1/4 do fade de entrada e no meio do de saída; o crossfade medido no começo, no meio (1,414, mesma potência) e no fim da sobra; o arrasto da alça para cima;
- o tom medido em Hz (+12 e -5 semitons) e o tempo que não muda; o reverso no som, cortado, de novo e com o arquivo apagado;
- normalizar o grupo junto e separado; a prévia do silêncio em -45 e -28 dB; tirar e abaixar, medidos no WAV; a ferramenta na tela;
- capturas: `voz-playlist-curvas.png` e `voz-playlist-silencio.png`.

## O que não foi feito

- O painel de propriedades do clipe, a ferramenta de zoom e as ações de marcador (a fatia 6, "se der tempo").
- O consolidar continua criando um canal novo (o WAV consolidado é outro arquivo).
- Limites conhecidos (da revisão independente):
  - um bloco que passa do fim do arquivo, invertido duas vezes, volta com o começo no zero (o trecho além do fim não tem espelho);
  - um canal invertido do jeito antigo ("X_invertido.wav") invertido de novo escreve "X_invertido_invertido.wav", em vez de voltar ao "X.wav";
  - "abaixar em vez de apagar" muda o ganho sem rampa nas bordas (o salto cai num trecho quieto);
  - o Ctrl+E num grupo de takes corta só o take escolhido (a ferramenta Cortar corta o grupo todo);
  - apagar e abrir espaço não mexem no punch in/out.
