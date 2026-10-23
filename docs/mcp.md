# A WHYNOT com uma IA (servidor MCP)

A WHYNOT LAB pode ser usada por uma IA, como o Claude Code ou o Claude Desktop, pelo servidor MCP que fica em `mcp/`.

A IA usa os mesmos comandos que a tela usa: criar canais, escrever passos e notas, mexer no mixer, arranjar a playlist, exportar. Cada coisa que ela muda vira um passo de desfazer chamado "IA: ...". O seu Ctrl+Z desfaz a IA como desfaz qualquer outra coisa.

O desenho e as decisões estão em `docs/fase-4-plano.md`.

## 1. Compilar o servidor (uma vez)

Precisa do Node 24 e do app compilado (`docs/como-rodar.md`, seção 3).

```
cd mcp
npm install
npm run build
```

O `npm install` baixa os pacotes da lista do `package.json`, todos com a versão exata. O `npm test` compila e roda os testes, inclusive contra o app sem janela.

## 2. Dois jeitos de usar

| Jeito | Como abre | Quem confirma o que é perigoso |
|---|---|---|
| **Com a janela** (recomendado) | Você abre a WHYNOT com `--control 47830`, e o servidor liga nela | Você, num cartão no alto da tela da WHYNOT |
| **Sem janela** | O servidor abre a WHYNOT escondida e a fecha quando ele sai | Você, numa pergunta do seu cliente de IA |

**Com a janela.** Crie um atalho para o app com este destino:

```
"<pasta do repositório>\build\whynot_lab_artefacts\Release\WHYNOT LAB.exe" --control 47830
```

O servidor liga nesse app com `node mcp\dist\src\index.js --attach 47830`. Você vê tudo o que a IA faz, na hora.

**Sem janela.** O servidor usa `node mcp\dist\src\index.js --headless`.
- Sem janela não há autosave: o projeto só fica salvo com `project.save` ou `project.saveNewVersion`. A IA sabe disso.
- Sem `--data <pasta>`, a WHYNOT escondida usa a sua pasta de dados de verdade (atalhos, pastas de plugin, favoritos).
- Se o servidor cair, a WHYNOT escondida fecha sozinha em poucos segundos.

## 3. Ligar no cliente de IA

A configuração do cliente é sua: faça você, ou peça e confirme.

**Claude Code** (no terminal, dentro da pasta do repositório):

```
claude mcp add whynot -s local -- node "<pasta do repositório>\mcp\dist\src\index.js" --attach 47830
```

Troque `--attach 47830` por `--headless` para o jeito sem janela. Para tirar: `claude mcp remove whynot -s local`. Uma conversa nova do Claude Code já vê as tools; a que estava aberta só vê depois de reiniciar.

**Claude Desktop**: no `%APPDATA%\Claude\claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "whynot": {
      "command": "node",
      "args": ["<pasta do repositório>\\mcp\\dist\\src\\index.js", "--attach", "47830"]
    }
  }
}
```

## 4. As tools

| Tool | O que faz |
|---|---|
| `command_list` | Os comandos do app. Sem filtro, a lista curta por domínio. Com `domain`, `query` ou `ids`, o schema dos parâmetros e o risco de cada um |
| `command_run` | Roda um comando. Os marcados [confirma] pedem o seu ok antes; os [bloqueado] são só pela tela |
| `command_batch` | Até 200 comandos num passo de desfazer só, tudo ou nada. Comando que pede confirmação recusa o lote |
| `get_project` | O projeto: `resumo` (texto curto, o padrão) ou `completo` (o JSON do `.wnl`, até 100 KB, com filtros) |
| `render_preview` | Renderiza o padrão ou a música na pasta da IA e devolve as medidas: pico, true peak, RMS, LUFS, amostras cortadas, silêncio e o RMS de cada compasso |
| `screenshot` | Uma captura da tela em PNG, até 1600 px. Com `view`, troca de tela antes (com a janela, você vê a troca) |

O `render_preview` mede, mas não ouve: ele diz se algo entrou, cortou ou ficou mudo. Quem ouve é você.

## 5. Segurança

O que a IA pode fazer depende de quem pede e de onde mexe.

- **Só neste computador.** A ponte ouve só em 127.0.0.1, com um token novo a cada vez que o app abre. Pedido de página da web (com `Origin`) ou de outro nome (DNS rebinding) é recusado. O servidor MCP fala com o cliente por stdio e não abre porta.
- **Três classes de comando** (a tabela fica em `src/control/CommandPolicy.cpp`):
  - **livre:** o que só muda o projeto, sempre com desfazer, e as leituras;
  - **pede o seu ok:** abrir ou escanear plugin VST3 (um plugin que cai derruba a WHYNOT), gravar por cima de um arquivo, perder o que não foi salvo, gravar fora do projeto (favoritos, a tag do produtor, os seus presets, a biblioteca) e qualquer caminho fora das pastas permitidas;
  - **só pela tela:** a placa de som, o microfone e a gravação, os atalhos, as pastas de plugin e desbloquear um plugin que já derrubou o app.
- **Pastas permitidas:** a pasta do projeto aberto, `Músicas\WHYNOT LAB` e a pasta da IA, para ler e gravar; o kit e as pastas extras do navegador de sons, só para ler.
- **A confirmação:**
  - com a janela, um cartão mostra o que a IA quer e o porquê, e espera até 2 min. O Permitir só acende depois de um instante, para um clique que já vinha não autorizar nada. Sem resposta, fica recusado;
  - sem a janela, o seu cliente de IA pergunta. O app dá um ticket de uso único, preso àquele comando e àqueles parâmetros, que vale 2 min;
  - se você recusa, a IA ouve que você recusou e não tenta de novo pelo mesmo caminho.
- **O registro:** cada pedido da IA fica numa linha do `ia.log` (seção 6): a hora, o comando, o resultado e os parâmetros. O token nunca entra.
- **Desligar a IA:** com a janela, o aviso "IA conectada" fica no pé, à direita. O "Desligar a IA" fecha a conexão e recusa as próximas até você clicar "Ligar a IA".
- **O que fica de fora:**
  - um programa rodando com o seu usuário do Windows consegue ler o token, como já consegue ler os seus projetos;
  - um plugin de terceiros roda dentro do app, e a confirmação antes de abrir é a única proteção;
  - nomes de canais, samples e projetos são tratados como dados, nunca como ordens para a IA.

## 6. Onde as coisas ficam

| O quê | Onde |
|---|---|
| Arquivo de controle (porta, token, pid) | `%APPDATA%\WHYNOT LAB\control\<porta>.json`; some ao fechar |
| Registro do que a IA fez | `%APPDATA%\WHYNOT LAB\control\ia.log`; com 1 MB vira `ia.anterior.log` |
| Pasta da IA (prévias e capturas) | `%LOCALAPPDATA%\WHYNOT LAB\IA\`; ao abrir, a ponte apaga o que tem mais de 24 h ou passa de 200 MB |

## 7. Problemas comuns

- **"A WHYNOT não está aberta com --control 47830"**: abra o app pelo atalho com `--control 47830`, ou use o servidor com `--headless`.
- **"Não consegui abrir a porta 47830"**: outro programa, ou outra WHYNOT, já está nela. Feche a outra, ou use outra porta nos dois lados.
- **A IA diz que o comando precisa da confirmação e que o cliente não pergunta**: o cliente não sabe perguntar (não tem elicitação). Use o jeito com a janela.
- **"Não consegui ligar na ponte da WHYNOT"**: o app fechou, ou alguém clicou "Desligar a IA". Abra o app de novo, ou clique "Ligar a IA" no pé da janela.
- **A IA não vê um comando novo**: a lista de comandos é lida quando o servidor liga. Reinicie a conversa do cliente de IA.
