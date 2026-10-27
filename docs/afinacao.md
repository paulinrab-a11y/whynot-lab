# Afinação (o auto-tune nativo da WHYNOT)

Frente B de 27/10, branch `extra-afinacao`. O efeito é o `tune` (`src/dsp/Tune.*`), na categoria Voz do mixer.
Clean-room: o comportamento foi pensado a partir do que se vê por fora em produtos do mercado (manuais e vídeos).
Nenhum nome comercial entra na interface nem nos presets.

## O que o efeito faz

| Controle | Faixa | O que faz |
|---|---|---|
| Tom e Escala | 12 tônicas; menor, maior, menor harmônica, frígio, dórico, pentatônica menor, cromática, **Personalizada** | As notas para onde a voz pode ir. "Seguir o tom do projeto" troca junto com o projeto |
| As 12 notas | ligada ou desligada, uma a uma | Na tela, a fileira de notas embaixo do gráfico. Clicar numa nota passa a escala para Personalizada (com as notas que já valiam) e desliga o "seguir" |
| Lá de referência | 430 a 450 Hz | A grade inteira de notas acompanha |
| Ajuste fino | -50 a +50 cents | Desloca todas as notas alvo |
| Velocidade | 0 a 400 ms | 0 = salto na hora (o robô); 50 a 150 ms soa natural |
| Força | 0 a 100% | Quanto da distância até a nota é corrigido |
| Humanizar | 0 a 100% | Nas notas longas a correção solta e o vibrato passa; a troca de nota continua rápida |
| Formante | -12 a +12 st | O timbre, à parte da nota. 0 = o timbre fica igual ao corrigir |
| Harmonia | Sem, +3ª, +5ª, -3ª, -8ª | Uma voz a mais, que sai da nota corrigida (já afinada): terça ou quinta da escala acima, terça abaixo ou a oitava exata abaixo |
| Nível da harmonia | 0 a 100% | |
| Seguir a guia | ligado ou desligado | A nota vem da trilha escolhida em "Guia" (sidechain), na oitava da voz |
| Voz | ampla, grave, aguda | A faixa de tom procurada |
| Mistura | 0 a 100% | A voz corrigida contra a original |

Presets: **Natural**, **Pop**, **Trap robótico** (id `hard`), **Harmonia**, Trap / funk (rápido), Robô grosso, Só um
toque, Duro com vibrato, Natural humanizado e Oitava abaixo. Os presets não trocam o tom, a escala, o "seguir" nem as
12 notas (nem os do artista, que também não guardam essas notas).

A tela mostra a nota que sai (grande), onde a voz está e quanto foi corrigido (em cents e na régua de -50 a +50),
o gráfico do tom da voz (cinza) e da saída (laranja) nos últimos 3 s sobre as linhas das notas, e a fileira das 12
notas, com a nota que soa em laranja.

## Decisão: o núcleo PSOLA e a latência de 58,5 ms

Antes de 27/10 o efeito deslocava o tom com duas cabeças de leitura numa linha de atraso (15 ms de latência). Assim as
formantes andavam junto com o tom e não havia como mexer no timbre.

Agora ele usa o `PsolaCore` (em `src/dsp/VoicePitch.*`, o mesmo do Tom da voz e da Harmonia). Cada grão guarda a forma
de onda de um período, então a voz muda de nota sem mudar de timbre.

| Medida (whynot_dsp_tests) | Núcleo antigo | PSOLA |
|---|---|---|
| Latência | 15 ms | **58,5 ms** (2808 amostras a 48 kHz) |
| Lá + 40 cents → Lá, pior janela de 100 ms | < 8 cents | 1,9 cents |
| Glissando 430-450 Hz, pior janela | < 10 cents | 1,6 cents |
| Modo duro com vibrato de ±40 cents | < 12 cents | ±5,4 cents |
| Correção de 2 st, formante em 1 kHz | o timbre sobe 2 st | o pico fica em 988 Hz (mantido) |
| Mistura 50%, voz já afinada | diferença de 0,2 para o seco (pente) | 47 dB abaixo da voz |

A latência é 3,5 períodos de 60 Hz mais 8 amostras: o grão pode ter até 2 períodos de meia largura (a formante em
-12 st), é montado 1,5 período depois da marca e lido meia largura antes dela. O `latencySamples()` informa esse valor
ao host, e o Tracktion compensa (PDC): no autoteste, a voz com o efeito começa no mesmo lugar que sem ele.

**Por que vale a latência maior:** ao gravar com a monitoração ligada, o app já tira da escuta os efeitos com latência
(`AppController::applyLowLatency`), e o take é seco. Então os 58,5 ms não chegam ao ouvido de quem canta, e na mixagem
o PDC compensa. Um modo de "baixa latência" com o núcleo antigo teria de trocar a latência informada com o efeito
rodando, e o `FxHost` fixa a latência no `prepare`. Ficou de fora.

Mudanças pequenas no `PsolaCore` (para a revisão do dono do Tom da voz):
- `prepare (rate, reads, maxGrainPeriods = 1.5f)`: o padrão é o de antes (mesma latência de 2408 amostras no Tom da voz
  e na Harmonia); a afinação pede 2;
- a meia largura do grão nunca passa do que cabe na latência;
- com razão e formante iguais a 1, o grão cai em cima da marca: a leitura fica em fase com a entrada.

## Decisão: a nota pelo MIDI

**MIDI direto num efeito de trilha de mixer não existe no motor hoje.** A trilha de mixer é uma `AudioTrack` sem clipes.
Os canais tocam nas próprias trilhas e mandam o áudio para ela. Mandar as notas de um canal do piano roll para o efeito
pediria clipes MIDI novos na trilha de mixer, no `Projector` (de outro dono), e cuidado com PAT, SONG, solo e
congelar.

No lugar, a **nota guia pelo sidechain**, que já funciona de ponta a ponta:
1. Ponha a melodia num canal de synth ou piano (no piano roll).
2. Na janela da afinação, escolha a trilha dele em "Guia" e ligue "Seguir a guia".
3. A voz vai para a nota que a guia toca, na oitava mais perto da voz, fora da escala também. Quando a guia para,
   volta para a escala.
4. Para não ouvir a guia, mande a saída da trilha dela para uma trilha com o fader no mínimo. O sidechain lê a trilha
   antes da saída. O autoteste confere que a guia não aparece no som.

Um segundo YIN mede a guia, e só roda com "Seguir a guia" ligado e uma trilha no sidechain. Ele ouve de 60 Hz a 2 kHz:
uma guia abaixo de 60 Hz (um 808 bem grave) não é medida, e a voz volta para a escala. Perto do trítono entre a voz
e a guia, a nota fica na oitava anterior (sem pular com o vibrato).

Próximo passo, se o usuário quiser o MIDI de verdade: o `Projector` copia os clipes MIDI do canal escolhido para a
trilha de mixer do efeito, e o `WhynotFxPlugin::applyToBuffer` lê o `bufferForMidiMessages`.

## Compatibilidade

- Projetos antigos abrem com os valores padrão nos parâmetros novos: todas as notas ligadas, Lá em 440 Hz, ajuste
  fino 0, formante 0, guia e harmonia desligadas. O som muda só pelo núcleo novo (mais preciso, timbre mantido) e pela
  latência, que o PDC compensa.
- A escala ganhou a opção 8 (Personalizada). Uma curva de automação antiga desenhada na escala guarda o valor
  normalizado, então o topo dela, que era Cromática, passa a ser Personalizada. É raro (quase ninguém automatiza a
  escala) e não foi convertido.
- Os ids dos presets antigos continuam. Natural, Pop e Robô ganharam valores novos (o Robô agora tem a formante -3):
  um projeto antigo com um deles abre com o preset marcado como alterado.

## Testes

- `whynot_dsp_tests`: `testTune`, `testTuneHumanize`, `testTuneScale` (12 notas uma a uma, Lá, ajuste fino),
  `testTuneFormant`, `testTuneSpeed` (velocidade, humanizar, troca sem estalo), `testTuneGuide` e `testTunePresets`.
- Autoteste, seções "Afinação: ...": o som no render e a compensação de latência, a escala pelos comandos e os presets,
  o Lá em 432 Hz, a formante, a guia num bus mudo e a tela (`tune-janela.png`, `tune-visual.png`).

Não testado: voz real cantada (só vozes sintéticas), ouvido humano, e a tela clicada com o mouse de verdade.
