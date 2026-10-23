# Servidor MCP da WHYNOT LAB

Deixa uma IA (Claude Code, Claude Desktop ou outro cliente MCP) usar os comandos do app, pela ponte local
`src/control/`. Segue a spec MCP 2026-07-28 em stdio e atende também clientes da 2025-11-25. O plano e as
decisões estão em `docs/fase-4-plano.md`.

## Compilar e testar

```
cd mcp
npm install
npm test
```

O `npm test` compila o TypeScript e roda os testes. Os testes de integração abrem o `WHYNOT LAB.exe` sem janela.
Eles usam o `build/` do repositório, ou o caminho em `WHYNOT_EXE`, e são pulados se o app não estiver compilado.

## Rodar

- **Com o app que você abriu:** abra a WHYNOT com `--control 47830` (um atalho na área de trabalho, por exemplo).
  O servidor liga nela:
  `node mcp/dist/src/index.js` (ou `--attach <porta>`).
- **Sem janela:** o servidor abre o próprio app, e o app fecha junto com o servidor:
  `node mcp/dist/src/index.js --headless [--exe <WHYNOT LAB.exe>] [--data <pasta>]`.
  - Sem `--data`, o app usa a sua pasta de dados de verdade, a mesma do app com janela (atalhos, pastas de plugin).
  - Sem janela não há autosave: o projeto só fica salvo com `project.save` ou `project.saveNewVersion`.

## Ligar no cliente de IA

Na configuração de servidores MCP do cliente (por exemplo, o `mcpServers` do Claude Desktop):

```json
{
  "mcpServers": {
    "whynot": {
      "command": "node",
      "args": ["<pasta do repositório>\\mcp\\dist\\src\\index.js", "--headless"]
    }
  }
}
```

No Claude Code: `claude mcp add whynot -- node "<pasta do repositório>\mcp\dist\src\index.js" --headless`.

## Tools

| Tool | O que faz |
|---|---|
| `command_list` | Os comandos do app: sem filtro, compactos por domínio; com `domain`, `query` ou `ids`, com o schema e o risco |
| `command_run` | Roda um comando. Os que mudam o projeto viram um passo de desfazer "IA: <título>"; os [confirma] pedem o ok da pessoa |
| `command_batch` | Até 200 comandos num passo de desfazer só, tudo ou nada |
| `render_preview` | Renderiza o padrão ou a música na pasta da IA e devolve as medidas do som (pico, LUFS, cortes, silêncio, RMS por compasso) |
| `screenshot` | Uma captura da tela em PNG (até 1600 px e ~1 MB), com ou sem janela |
| `get_project` | O projeto: `resumo` (texto curto) ou `completo` (o JSON do `.wnl`, com filtros) |

A confirmação: com a janela, a WHYNOT pergunta num cartão na tela; sem janela, o `command_run` devolve
`input_required` com uma elicitação, e o ticket do app viaja no `requestState` assinado. Os bloqueados (placa de som,
atalhos, gravação...) são só pela tela. O guia de uso, com a segurança, está em `docs/mcp.md`.
