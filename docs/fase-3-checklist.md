# Fase 3 (MVP): checklist

Estado em 16/10/2026, no `main` (a 3.4 em c905ade e o autoteste por área em d335198).

O que foi rodado para conferir:
- **Autoteste**: `--self-test`, 205 verificações ok, código de saída 0.
- **Núcleo de tempo real e DSP**: `ctest`, 2 de 2.
- **pluginval nível 5**: passam os 2 plugins de teste que funcionam (dos 4 que o build compila, dois quebram de propósito), o Surge XT e o Surge XT Effects.
- **Teste com mouse e teclado** no app aberto.

Como ler a tabela:
- "Autoteste" = medido sem janela (áudio comparado amostra por amostra, frequências, níveis).
- "No app" = clicado e conferido na tela.

| Item do MVP | Estado | Como foi conferido | Tela |
|---|---|---|---|
| Transporte (tocar, parar, BPM, compasso, PAT/SONG, loop, metrônomo) | feito | Autoteste (duração exata do WAV a 140 BPM) e no app | `3.1-tela-principal.png` |
| Channel rack com 16 passos (velocity, roll, cortar a si mesmo) | feito | Autoteste e no app | `3.2-channel-rack.png` |
| Piano roll (desenhar, mover, redimensionar, apagar, velocity, grade, pintar, glide do 808) | feito | Autoteste (65,4 → 98,0 Hz no slide) e no app | `3.2-piano-roll-808.png` |
| Playlist (padrões e clipes de áudio, modo song) | feito | Autoteste (música com compasso vazio e clipe) e no app | `3.2-playlist.png` |
| Mixer com 8 inserts (efeitos nativos e VST3, envios, sidechain) | feito | Autoteste e no app | `3.3-mixer-trap.png`, `3.3-surge-fx-no-insert.png` |
| Navegador de plugins com arrastar (F8) | feito | Autoteste (scan isolado, bloqueio) e no app com o Surge XT | `3.3-navegador-e-surge-no-rack.jpg` |
| Navegador de sons com arrastar (Alt+F8) | feito | Autoteste (prévia pelo callback de áudio). No app: abre, duplo clique cria o canal | `3.4-navegador-de-sons.png` |
| Salvar e abrir `.wnl` (coleta de samples, versões, autosave) | feito | Autoteste (reabrir dá o mesmo WAV; versões `_2`/`_3`; autosave; queda simulada). No app: Ctrl+N, autosave sozinho em 2 min, queda e recuperação | `3.4-recuperar-autosave.jpg` |
| Exportar WAV, MP3 e MIDI (e stems) | feito | Autoteste (MP3 decodificado de volta; MIDI relido; soma dos stems = mixagem, resíduo -127 dB). No app: MP3 exportado pela tela | `3.4-exportar-mp3.png`, `3.4-exportar-stems.jpg` |
| Configuração de áudio e MIDI (WASAPI e ASIO) | feito | Autoteste (ASIO entre os drivers). No app: F10 com driver, controladores e teclado-piano | `3.4-audio-e-midi.png` |
| Atalhos estilo FL | feito | Autoteste (presets FL e WHYNOT, troca de tecla, `atalhos.json`, Ctrl+Z alterna). No app: editor, troca e volta ao preset | `3.4-atalhos.png` |

## O que não foi testado de verdade

- **Controlador MIDI e placa ASIO**: não há hardware aqui.
  - O app lista as entradas e liga e desliga cada uma. O roteamento para o canal escolhido usa o caminho do Tracktion e roda sem erro, mas nenhuma nota de um controlador real passou por ele.
  - O tipo ASIO aparece; sem driver ASIO instalado não há aparelho para escolher.
- **Ouvir**: o teclado-piano, a prévia do navegador e o MP3 não foram ouvidos.
  - A prévia foi medida no callback de áudio (-9 dBFS, cala em menos de 50 ms ao parar).
  - O MP3 foi medido decodificado.
  - O teclado-piano usa o mesmo caminho da prévia de nota do piano roll.
- **Clique simples no navegador de sons**: não confirmado na tela. No teste, a janela do Claude ficou por cima e os cliques caíram nela. O duplo clique e a prévia pelo autoteste funcionaram.
- **Alt+F8**: a ferramenta de teste bloqueia a combinação. O mapeamento está no autoteste, e o mesmo painel abre pelo trilho.

## Limitações conhecidas

- **Stem de trilha com sidechain**: no stem do 808, o kick que dispara o compressor dele fica mudo, então o 808 sai sem o ducking. É a mesma regra do solo.
- **MIDI**: o glide do 808 sai como notas sobrepostas (legato), não como pitch bend.
- **MP3 decodificado pelo JUCE**: tem 45 ms a mais no começo, que é o atraso do codificador. Players que leem o quadro do LAME descontam. O MP3 também não limita o pico real a -1 dBTP.
- **Clipe de áudio na playlist**: corrigido depois do checklist. O corte do começo agora fica em segundos do arquivo, no esquema 3 do `.wnl`, e mudar o BPM não muda mais o trecho que toca. O clipe não estica: em BPM mais lento, ele toca mais do arquivo no mesmo compasso.
- **Gravação de áudio e de MIDI, e automação**: ficaram fora das quatro entregas e vieram depois, na 3.5 (`docs/gravacao.md`), já no `main`.
