# Fase 4 (servidor MCP): checklist

Estado em 23/10/2026, no `main`: as fatias 4.1 a 4.3 em e00616f, a 4.5 em 03cc0ac e a 4.6 em 0fef841. A 4.4 entrou junto da 4.2 e da 4.3 (o lote e o desfazer da IA).

O que foi rodado para conferir:
- **Autoteste**: `--self-test`, 1315 verificações ok, código de saída 0. A seção "Ponte de controle da IA (Fase 4)" tem as da ponte.
- **ctest**: 10 de 10, com o `control` (protocolo, regras de caminho, tabela de risco e fuzz de 100 mil entradas, sem JUCE).
- **Servidor MCP**: `npm test` em `mcp/`, 27 de 27. Quatro deles rodam contra o app sem janela, com o cliente oficial nas duas versões da spec (2026-07-28 e 2025-11-25).
- **Cliente de verdade** (21/10): o Claude Code ligado no servidor mudou o BPM de 140 para 150, com o desfazer "IA: Mudar BPM", e o comando da placa de som voltou recusado.
- **Com você, na janela** (23/10, 11:44 a 11:45): a WHYNOT aberta com `--control 47830` e o servidor MCP novo (`--attach 47830`), com um roteiro no papel da IA pedindo para gravar por cima de um WAV na pasta da IA. Você clicou Permitir, e o export rodou; clicou Recusar, e a IA ouviu "A pessoa recusou na janela da WHYNOT". As duas respostas estão no `ia.log`.

Como ler a tabela:
- "Autoteste" e "npm test" = medido sem você (a ponte de verdade, com um cliente de teste).
- "Com você" = clicado ou conferido por você, na tela.

| Item | Estado | Como foi conferido | Tela |
|---|---|---|---|
| Ponte só em 127.0.0.1, com token, `Host` e `Origin` | feito | Autoteste: sem token e token errado dão 401; com `Origin` e com outro `Host`, 403; o IP da rede local não liga | — |
| Protocolo WebSocket e JSON-RPC (limites, fila, ping, conexões) | feito | ctest (vetores das RFCs e fuzz) e autoteste: 2 MB fecha com 1009, 40 pedidos dão 32 respondidos e 8 "ocupado", `params` em lista dá -32602 | — |
| Os comandos pela IA, com desfazer "IA: ..." | feito | Autoteste e npm test: BPM pela ponte, o `edit.undo` só desfaz o que é da IA | — |
| Lote num passo de desfazer, tudo ou nada | feito | Autoteste: o lote com o 2º passo errado não deixa nada; o certo vira um passo só | — |
| O seu gesto vem primeiro | feito | Autoteste: com um gesto aberto, o comando espera e roda depois; gesto que não acaba, desiste | — |
| Tabela de risco com todos os comandos (220) | feito | Autoteste: "todo comando registrado tem classe de risco para a IA" | — |
| Pastas permitidas | feito | ctest (caminhos, junction, UNC, `CON`...) e autoteste: kit liberado, arquivo de fora pede confirmação, caminho relativo recusado | — |
| Confirmação na janela | feito | Autoteste, com um `askUser` de teste: Permitir, Recusar, sem resposta no prazo, a IA desiste, a IA cai e Desligar a IA. Com você: Permitir e Recusar clicados na janela | `4.5-ia-confirmacao.png` |
| Confirmação pelo cliente de IA (sem janela) | feito | Autoteste (o ticket: de uso único, preso ao comando e aos parâmetros, com prazo) e npm test (a elicitação nos dois tipos de cliente; cliente sem elicitação recusado) | — |
| `ia.log`, "IA conectada" e "Desligar a IA" | feito | Autoteste: as linhas, o corte em 200 caracteres, o 1 MB, o token fora; desligar dá 1008 e 503, ligar volta | `4.5-ia-confirmacao.png` (o aviso no pé) |
| Ouvir: `render_preview` com as medidas | feito | Autoteste com sons conhecidos (seno de 1 kHz a 0,5: -6,02 dBFS, RMS -9,03, -6,01 LUFS; um som que corta, com silêncio) e com o beat; npm test contra o app | — |
| Ver: `screenshot` com e sem janela | feito | Autoteste: a tela montada fora da área de trabalho (900x562, 43 KB); npm test: o PNG chega à IA | `4.6-ia-captura-sem-janela.png` |
| Servidor MCP: as 6 tools, as `instructions`, o app sem janela que fecha junto | feito | npm test: `server/discover`, as tools em ordem, o app fecha quando o stdin fecha e apaga o arquivo de controle | — |
| Guia de uso | feito | `docs/mcp.md` | — |

## O que não foi testado de verdade

- **O seu clique em "Desligar a IA"** no pé da janela. O autoteste testa o botão por código; o clique na tela ainda não foi feito.
- **Uma tarefa de verdade com a IA** (seção 6.5 do plano): "monte um beat de trap de 4 compassos, ouça e ajuste", com o seu Ctrl+Z desfazendo a IA. Também fica para o fim, com você.
- **O Claude Code usando as fatias 4.5 e 4.6.** Desde 23/10 (com o seu ok), o servidor `whynot` do Claude Code aponta para o código novo, no jeito com a janela (`--attach 47830`). Uma conversa nova do Claude Code vê as 6 tools; nenhuma conversa as usou ainda.
- **O Claude Desktop.** A configuração está no guia, mas não foi ligada aqui.
- **Ouvir.** O `render_preview` mede o som: pico, LUFS, cortes, silêncio, RMS por compasso. Ninguém ouviu as prévias.
- **Plugins de terceiros pela IA.** Abrir um VST3 pede confirmação. O autoteste confere o pedido, mas nenhum plugin real foi aberto pela IA depois de um Permitir.
