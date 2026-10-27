# Afinação nota a nota

Frente C de 27/10, branch `extra-afina-nota`. A voz de um bloco de áudio da playlist aparece como blocos de nota sobre
um teclado, e cada nota pode ser movida, centralizada, ter o vibrato achatado, ser cortada ou juntada. O comportamento
foi pensado a partir do que se vê por fora nos editores de afinação do mercado (manuais e vídeos). Nenhum código, nome
ou tela de outro programa entra aqui.

## Para quem edita

Na setinha do cabeçalho do bloco de áudio: **Afinar nota a nota...** (o botão direito na grade da playlist apaga, como
no resto do app; por isso o item fica no menu da setinha). Abre uma janela com:

- à esquerda, o teclado; as linhas da escala do projeto mais claras e a tônica mais clara ainda;
- os **blocos de nota** na nota para onde vão, com o nome e quantos cents ficam fora do semitom. Uma nota movida deixa
  um contorno cinza onde estava;
- a **curva fina** do tom: a branca é a que sai; a cinza aparece onde a edição mudou alguma coisa (é a cantada);
- embaixo, a forma de onda, com o trecho das notas escolhidas em laranja, e a agulha quando toca.

| Gesto | O que faz |
|---|---|
| Clique numa nota | Escolhe (Shift ou Ctrl soma). Clique no vazio tira a escolha |
| Arrastar para cima ou para baixo | Move as notas escolhidas, caindo no semitom. Com **Alt**, fino (em cents) |
| Setas ↑ ↓ | Um semitom (Ctrl: uma oitava; Alt: 10 cents) |
| Duplo clique numa nota, ou a ferramenta **Cortar** (C) | Corta a nota ali (cada pedaço com 30 ms ou mais) |
| **Juntar** (J) | Junta as notas escolhidas que estão encostadas |
| **Escala do projeto** | Cada nota para a nota da escala do projeto mais perto, no centro do semitom |
| **Voltar ao original** | Tira todas as edições (o arquivo original nunca muda) |
| Delete | Tira as edições das notas escolhidas |
| **Centralizar** (0 a 100%) | Puxa a nota para o centro do semitom e tira o desvio lento (a nota "torta") |
| **Achatar vibrato** (0 a 100%) | Tira o desvio rápido, sem mexer na nota |
| **Transição** (0 a 300 ms) | Quanto a correção leva para entrar no começo da nota: 0 = salto; mais = desliza |
| **Formante** (-12 a +12 st) | O tamanho da voz, à parte da nota |
| **Timbre junto com o tom** | Ligado: o timbre sobe e desce com a nota, como fita. Desligado (padrão): o timbre fica |
| **Tocar** (Espaço) | Toca a música do começo do trecho que está na tela, com a afinação |
| Ctrl+roda, Shift+roda, roda | Zoom no tempo, andar no tempo, andar nas notas |
| Ctrl+Z / Ctrl+Y | Desfazer e refazer (os mesmos da janela principal) |

Cada gesto é um passo de desfazer: o arrasto e os controles gravam ao soltar.

## Para a IA (comandos)

| Comando | O que faz |
|---|---|
| `audio.analyzeNotes {item}` | As notas que a análise acha (`analysis`: início, fim em segundos do arquivo, nota MIDI, desvio e vibrato em cents) e as que estão no projeto (`notes`). Não muda nada |
| `audio.editNotes {item, notes?, to?, semitones?, center?, flatten?, formant?, transitionMs?, formantFollows?}` | Edita as notas dos índices pedidos (todas sem `notes`). `to` leva para uma nota MIDI; `center` e `flatten` de 0 a 100 |
| `audio.splitNote {item, index, at}` | Corta a nota em `at` segundos do arquivo |
| `audio.mergeNotes {item, index}` | Junta a nota com a seguinte |
| `audio.tuneToScale {item, strength?}` | Tudo para a escala do projeto (força de 0 a 100, padrão 100) |
| `audio.setNoteTune {item, notes, formantFollows?}` | Troca todas as notas de uma vez (é o que a tela usa) |
| `audio.clearNoteTune {item}` | Volta ao original |

Todos têm desfazer, menos o `analyzeNotes`. Um bloco invertido é recusado (o arquivo invertido é outro).

## Como funciona por dentro

- **Análise** (`src/dsp/NoteTune.cpp`, sem JUCE): YIN a ~12 kHz a cada 5 ms (de 60 Hz a 1 kHz), refinado na taxa cheia
  perto do período achado, com interpolação de parábola. Quadros sem tom: aperiódicos (diferença normalizada acima de 0,25)
  ou mais de 35 dB abaixo do mais alto. Saltos de oitava isolados voltam, uma mediana de 5 quadros alisa, e trechos com
  tom de menos de 15 ms saem.
- **Notas:** os trechos com tom (buracos de até 30 ms não separam) são cortados onde a média de 80 ms sai mais de
  0,75 semitom da nota por 40 ms; o corte cai onde o tom passou da metade do caminho. Notas de menos de 60 ms juntam com
  a vizinha encostada de tom mais perto. A nota média é a mediana do miolo (sem 15% de cada ponta); o desvio lento é a
  média móvel de 150 ms em volta dela, e o vibrato é o resto.
- **Cache por arquivo** (`src/record/NoteTuneFiles.*`): na memória e no disco, na pasta temporária
  (`%TEMP%\WHYNOT LAB\notas`), pela chave caminho + tamanho + data. Um arquivo regravado é analisado de novo.
- **A correção** em cada quadro: alvo = nota (movida e centralizada) + (1 − centralizar) × desvio lento + (1 − achatar)
  × vibrato; a correção é o alvo menos o tom cantado. Sem edição, ela é zero exato. No começo de uma nota encostada na
  anterior, a correção vai da anterior até a nova no tempo da transição.
- **Render não destrutivo:** só os quadros com correção passam pelo `PsolaCore` (o mesmo da afinação e do Tom da voz,
  com o grão de 2 períodos, a formante até -12 st e 58,5 ms de latência, que o render compensa). O tom de cada grão sai da
  correção na posição dele. Os trechos refeitos ganham 25 ms de folga (onde a correção já é zero) e 10 ms de crossfade nas
  bordas, com o PSOLA em fase com a entrada. **Fora deles, a saída é a entrada amostra por amostra.** Um NaN no arquivo
  entra no PSOLA e na análise como silêncio.
- **O arquivo refeito** fica ao lado do original, `<nome>_nota_<código>.wav` (WAV 32 bits float). O código sai das notas,
  então cada versão das edições tem o arquivo dela e o desfazer volta a uma que já existe. Ficam as 6 versões mais novas
  por arquivo. Se o refeito sumir (projeto copiado sem ele), o `Projector` escreve de novo a partir do original, com os
  mesmos bytes.
- **A prévia:** o comando escreve o arquivo refeito antes de mudar o projeto; o `Projector` troca o arquivo do clipe na
  mesma hora, então tocando ou parado ouve-se a edição de agora.
- **MP3:** a leitura tira o atraso do codificador do começo (o mesmo que o clipe da playlist pula), então a análise, o
  editor e o arquivo refeito ficam no tempo do bloco.
- **Render que falhou** (pasta sem escrita, disco cheio): o bloco toca o original, o pé avisa, e o `Projector` não tenta
  de novo a cada mudança do projeto nesta execução (o comando, sim: ele mostra o erro).
- **Trocar o sample do canal** (`channel.setSample`) tira as notas: elas eram do áudio antigo.
- O editor analisa numa thread dele e para a análise ao fechar (a análise confere um sinal a cada 256 quadros).

## No arquivo `.wnl`

O esquema não muda. As notas ficam no canal de áudio (`"noteTune"`), só quando há notas, iguais no `toJson` e no
`toJsonReference`, em inteiros para o projeto reaberto voltar valor por valor:

```json
"noteTune": {
  "formantFollows": true,
  "notes": [
    { "start10us": 70800, "end10us": 121300, "sourceCents": 6000, "shiftCents": 200, "centerPct": 100,
      "flattenPct": 50, "formantCents": -300, "transitionMs": 80 }
  ]
}
```

Os tempos vão em inteiros de 10 µs (até 5 h, para caber no `int`); `shiftCents`, `centerPct`, `flattenPct`, `formantCents` e `transitionMs`
só saem quando diferentes do padrão (0, 0, 0, 0 e 40).

## Decisões

- **As notas ficam no canal, não no bloco.** Os pedaços de um bloco cortado tocam o mesmo arquivo; com as notas no
  canal (em segundos do arquivo), os pedaços continuam afinados. Uma cópia do bloco no refrão também toca afinada: é a
  mesma gravação.
- **Render no comando, não na thread de áudio.** O PSOLA é previsível e barato (um trecho de 0,5 s em ~8 ms no teste),
  então o comando escreve o arquivo e o motor só lê um WAV.
- **Bloco invertido fica de fora.** O arquivo invertido é outro; afinar os dois juntos fica para depois, se pedirem.

## Testes

- `whynot_notetune_tests` (ctest `notetune`, sem JUCE): as 5 notas de uma voz sintética (média a 2,6 cents, pontas a
  12,5 ms), o vibrato só no Mi, a curva a 0,003 cents no meio da nota; cortar, juntar, mover, escala e `normalise`; o
  render sem edição igual à entrada; o Dó movido sai em Ré a 0,2 cents; fora dele, igual bit a bit; sem estalo; estéreo
  igual ao mono; centralizar 100% e 50%; achatar o vibrato (de 26 para 6 cents de desvio); a formante +4 st (2º/3º
  harmônico); a transição; silêncio, ruído e NaN; o `normalise` que dá o mesmo rodando duas vezes.
- Autoteste, seções "Afinação nota a nota: ...": a análise e o cache (feito, disco, memória); as edições e o desfazer;
  o render no export (Hz da nota movida, diferença máxima fora dela, o original com os mesmos bytes, o `.wnl` e o
  arquivo refeito de novo); a formante junto e separada e a prévia; o editor sem janela (arrasto com o mouse, controle
  que grava ao soltar, cortar, juntar, escala) e as capturas `nota-a-nota-editor.png` e `nota-a-nota-escala.png`;
  trocar o sample tira as notas; bloco invertido recusado.

Não testado: voz real cantada (só a voz sintética), ouvido humano, a janela clicada com o mouse de verdade (o
editor foi testado sem janela, com eventos de mouse montados), um MP3 de verdade na afinação nota a nota e um arquivo
longo (minutos) no editor.
