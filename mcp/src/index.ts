#!/usr/bin/env node
// Servidor MCP da WHYNOT LAB (Fase 4), pela spec 2026-07-28, em stdio: o cliente de IA abre este processo,
// e ele fala com o app pela ponte local. O stdout é só do MCP; o log vai para o stderr.
//
//   node dist/src/index.js [--attach <porta>]   liga no app aberto com --control <porta> (padrão 47830)
//   node dist/src/index.js --headless [--exe <WHYNOT LAB.exe>] [--data <pasta>] [--control-file <json>]
//                                               abre o app sem janela; ele fecha junto com este processo

import { McpServer, createRequestStateCodec } from '@modelcontextprotocol/server';
import { randomBytes } from 'node:crypto';
import { serveStdio } from '@modelcontextprotocol/server/stdio';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { Bridge } from './bridge.js';
import { INSTRUCTIONS, registerTools, type ConfirmState } from './tools.js';

const args = process.argv.slice(2);
const option = (name: string) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const log = (line: string) => process.stderr.write(`[whynot-mcp] ${line}\n`);

// do mcp/dist/src até o build/ do repositório
const here = dirname(fileURLToPath(import.meta.url));
const defaultExe = resolve(here, '..', '..', '..', 'build', 'whynot_lab_artefacts', 'Release', 'WHYNOT LAB.exe');

const bridge = new Bridge({
  mode: args.includes('--headless') ? 'headless' : 'attach',
  port: Number(option('--attach') ?? option('--port') ?? 47830),
  exe: option('--exe') ?? process.env.WHYNOT_EXE ?? defaultExe,
  dataFolder: option('--data'),
  controlFile: option('--control-file'),
  log,
});

// a confirmação sem janela (seção 5.5): o ticket do app viaja no requestState assinado por uma chave deste
// processo, que vale 2 min (o mesmo prazo do ticket). Quem decide se ele vale é o app.
const codec = createRequestStateCodec<ConfirmState>({ key: randomBytes(32), ttlSeconds: 120 });

const handle = serveStdio(() => {
  const server = new McpServer({ name: 'whynot-lab', version: '0.1.0' }, {
    capabilities: { tools: {} },
    instructions: INSTRUCTIONS,
    cacheHints: {
      'tools/list': { ttlMs: 3_600_000, cacheScope: 'private' },   // a lista de tools é fixa
      'server/discover': { ttlMs: 3_600_000, cacheScope: 'private' },
    },
    maxToolInputElements: 100_000,
    requestState: { verify: (state, ctx) => codec.verify(state, ctx) },
  });

  registerTools(server, bridge, codec);
  return server;
}, { onerror: (error) => log(`erro: ${error.message}`) });

// O stdin fechado é o sinal de sair (spec, stdio). Fecha a ponte e, no modo sem janela, o app.
let closing = false;

const shutdown = async () => {
  if (closing)
    return;

  closing = true;
  await bridge.close().catch(() => {});
  await handle.close().catch(() => {});
  process.exit(0);
};

process.stdin.on('end', shutdown);
process.stdin.on('close', shutdown);
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
log(`pronto (${args.includes('--headless') ? 'sem janela' : `ligando no --control ${option('--attach') ?? option('--port') ?? 47830}`})`);
