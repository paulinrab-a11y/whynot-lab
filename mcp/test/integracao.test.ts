// O servidor MCP contra o app de verdade, sem janela: o cliente oficial (@modelcontextprotocol/client) abre o
// servidor por stdio, e o servidor abre o WHYNOT LAB.exe com --headless. Precisa do app compilado
// (WHYNOT_EXE, ou o build/ do repositório); sem ele, o teste é pulado.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

const here = dirname(fileURLToPath(import.meta.url));
const serverJs = resolve(here, '..', 'src', 'index.js');
const exe = process.env.WHYNOT_EXE ?? resolve(here, '..', '..', '..', 'build', 'whynot_lab_artefacts', 'Release', 'WHYNOT LAB.exe');
const skip = existsSync(exe) ? false : `sem o app compilado em ${exe}`;

const sleep = (ms: number) => new Promise((done) => setTimeout(done, ms));
const alive = (pid: number) => { try { process.kill(pid, 0); return true; } catch { return false; } };
const textOf = (r: any) => r.content?.[0]?.text as string;

async function gone(pid: number, ms: number) {
  const until = Date.now() + ms;

  while (alive(pid) && Date.now() < until)
    await sleep(100);

  return !alive(pid);
}

/** A memória privada do processo (o working set do Windows encolhe com o app parado e engana). */
function memoryMb(pid: number): number {
  const out = execFileSync('powershell', ['-NoProfile', '-Command', `(Get-Process -Id ${pid}).PrivateMemorySize64`], { encoding: 'utf8' });
  return Math.round(Number(out.trim()) / (1024 * 1024));
}

async function start(versionNegotiation?: object, elicit?: (message: string) => object) {
  const dir = mkdtempSync(join(tmpdir(), 'whynot-mcp-teste-'));
  const controlFile = join(dir, 'control.json');
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [serverJs, '--headless', '--exe', exe, '--data', join(dir, 'dados'), '--control-file', controlFile],
    stderr: 'inherit',
  });
  const options: any = versionNegotiation ? { versionNegotiation } : {};

  // um cliente que pergunta à pessoa (elicitação): aqui a "pessoa" é a função do teste
  if (elicit)
    options.capabilities = { elicitation: { form: {} } };

  const client = new Client({ name: 'teste-whynot', version: '1.0.0' }, options);

  if (elicit)
    client.setRequestHandler('elicitation/create', async (request: any) => elicit(request.params.message) as any);

  await client.connect(transport);
  return { client, transport, dir, controlFile };
}

test('cliente 2026-07-28: discover, tools, comandos, resumo, lote e os recusados', { skip, timeout: 180000 }, async () => {
  const { client, transport, dir, controlFile } = await start({ mode: { pin: '2026-07-28' } });

  try {
    assert.equal(client.getNegotiatedProtocolVersion(), '2026-07-28');

    const discover = await client.discover();
    assert.ok(discover.supportedVersions.includes('2026-07-28'), `versões: ${discover.supportedVersions}`);
    assert.match(String(discover.instructions ?? client.getInstructions() ?? ''), /WHYNOT LAB é uma DAW/);

    const list = await client.listTools();
    assert.deepEqual(list.tools.map((t) => t.name), ['command_list', 'command_run', 'command_batch', 'render_preview', 'screenshot', 'get_project']);
    assert.equal(list.tools[0].annotations?.readOnlyHint, true);
    assert.equal(list.tools[1].annotations?.destructiveHint, false);

    // a primeira tool liga a ponte e abre o app sem janela
    const t0 = performance.now();
    const transportCommands = await client.callTool({ name: 'command_list', arguments: { domain: 'transport' } });
    const firstMs = Math.round(performance.now() - t0);
    const ids = (transportCommands.structuredContent as any).commands.map((c: any) => c.id);
    assert.ok(ids.length >= 9 && ids.every((id: string) => id.startsWith('transport.')), `transporte: ${ids}`);

    const t1 = performance.now();
    const bpm = await client.callTool({ name: 'command_run', arguments: { id: 'transport.setBpm', params: { bpm: 150 } } });
    const commandMs = (performance.now() - t1).toFixed(1);
    assert.ok(!bpm.isError, textOf(bpm));
    assert.equal((bpm.structuredContent as any).undo, 'IA: Mudar BPM');

    const summary = await client.callTool({ name: 'get_project', arguments: {} });
    assert.match(textOf(summary), /^Projeto "[^"]+" · 150 BPM · 4\/4/);
    assert.match(textOf(summary), /Canais \(6\):[\s\S]*"808"/);
    assert.match(textOf(summary), /sem janela: não há autosave/);

    const batch = await client.callTool({ name: 'command_batch', arguments: { title: 'Lote do teste', steps: [
      { id: 'transport.setBpm', params: { bpm: 96 } },
      { id: 'project.rename', params: { name: 'Feito pelo MCP' } }] } });
    assert.ok(!batch.isError, textOf(batch));
    assert.equal((batch.structuredContent as any).undo, 'IA: Lote do teste');

    const full = await client.callTool({ name: 'get_project', arguments: { view: 'completo', sections: ['project'] } });
    const project = JSON.parse(textOf(full));
    assert.equal(project.project.bpm, 96);
    assert.equal(project.project.name, 'Feito pelo MCP');
    assert.equal(project.channels, undefined);

    // ouvir e ver (fatia 4.6), com o app sem janela
    const preview = await client.callTool({ name: 'render_preview', arguments: { loops: 1 } });
    assert.ok(!preview.isError, textOf(preview));
    assert.match(textOf(preview), /^Prévia do padrão \(1 volta\): /);
    assert.ok((preview.structuredContent as any).seconds > 1, 'a prévia tem som de pelo menos um compasso');

    const shot = await client.callTool({ name: 'screenshot', arguments: { view: 'playlist', width: 800 } });
    assert.ok(!shot.isError, textOf(shot));
    const image = (shot.content as any[])[0];
    assert.equal(image.type, 'image');
    assert.equal(Buffer.from(image.data, 'base64').subarray(1, 4).toString('latin1'), 'PNG', 'é um PNG');
    assert.match((shot.content as any[])[1].text, /Tela "playlist": 800x500 px/);

    // o piano roll de um canal que a IA escolhe (teste 6.5 da Fase 4: antes, só o canal que estava escolhido)
    const channels = JSON.parse(textOf(await client.callTool({ name: 'get_project', arguments: { view: 'completo', sections: ['channels'] } }))).channels as any[];
    const bass = channels.find((c) => c.name === '808');
    const roll = await client.callTool({ name: 'screenshot', arguments: { channel: bass.id, width: 800 } });
    assert.ok(!roll.isError, textOf(roll));
    assert.match((roll.content as any[])[1].text, new RegExp(`^Tela "pianoRoll", canal "808" \\(${bass.id}\\): 800x500 px`));

    const noChannel = await client.callTool({ name: 'screenshot', arguments: { view: 'pianoRoll', channel: 'ch_inventado' } });
    assert.ok(noChannel.isError);
    assert.match(textOf(noChannel), /Canal desconhecido: ch_inventado\. Os canais com piano roll: .*808/);

    const blocked = await client.callTool({ name: 'command_run', arguments: { id: 'audio.setDevice', params: { type: 'ASIO' } } });
    assert.ok(blocked.isError);
    assert.match(textOf(blocked), /só para você, pela tela/);

    const confirm = await client.callTool({ name: 'command_run', arguments: { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } } });
    assert.ok(confirm.isError);
    assert.match(textOf(confirm), /Precisa da confirmação da pessoa/);

    const wrong = await client.callTool({ name: 'command_run', arguments: { id: 'transport.setBpm', params: { bpm: 'rápido' } } });
    assert.ok(wrong.isError);
    assert.match(textOf(wrong), /"bpm" precisa ser number/);

    const undo = await client.callTool({ name: 'command_run', arguments: { id: 'edit.undo' } });
    assert.ok(!undo.isError, textOf(undo));
    assert.equal(JSON.parse(textOf(await client.callTool({ name: 'get_project', arguments: { view: 'completo', sections: ['project'] } }))).project.bpm, 150,
                 'o edit.undo desfaz o lote inteiro');

    const appPid = JSON.parse(readFileSync(controlFile, 'utf8')).pid as number;
    const serverPid = transport.pid!;
    const appMb = memoryMb(appPid), serverMb = memoryMb(serverPid);
    console.log(`    a primeira tool (abre o app) em ${firstMs} ms; um comando em ${commandMs} ms; memória privada: app sem janela ${appMb} MB, servidor MCP ${serverMb} MB`);
    assert.ok(appMb + serverMb < 1024, 'o app sem janela e o servidor juntos ficam abaixo de 1 GB');

    await client.close();
    assert.ok(await gone(serverPid, 10000), 'o servidor sai quando o stdin fecha');
    assert.ok(await gone(appPid, 10000), 'o app sem janela fecha junto');
    assert.ok(!existsSync(controlFile), 'e apaga o arquivo de controle');
  } finally {
    await client.close().catch(() => {});
    rmSync(dir, { recursive: true, force: true });
  }
});

test('cliente antigo (2025-11-25, com initialize) também lista e usa as tools', { skip, timeout: 180000 }, async () => {
  const { client, transport, dir, controlFile } = await start();

  try {
    assert.notEqual(client.getNegotiatedProtocolVersion(), '2026-07-28');
    const list = await client.listTools();
    assert.deepEqual(list.tools.map((t) => t.name), ['command_list', 'command_run', 'command_batch', 'render_preview', 'screenshot', 'get_project']);

    const summary = await client.callTool({ name: 'get_project', arguments: {} });
    assert.match(textOf(summary), /^Projeto "[^"]+" · 140 BPM/);

    // 4 compassos a 140 BPM em 2 voltas: exatamente 8 compassos, sem o 9º "fantasma" de 1 amostra (teste 6.5 da Fase 4)
    const length = await client.callTool({ name: 'command_run', arguments: { id: 'pattern.setLength', params: { steps: 64 } } });
    assert.ok(!length.isError, textOf(length));
    const preview = await client.callTool({ name: 'render_preview', arguments: { what: 'pattern', loops: 2 } });
    assert.ok(!preview.isError, textOf(preview));
    const m = preview.structuredContent as any;
    assert.equal(m.bpm, 140);
    assert.equal(m.barsRmsDb.length, 8, `compassos: ${m.barsRmsDb.join(', ')}`);
    assert.equal(m.lastBarPartial, false);
    assert.deepEqual(m.mutedBars, m.barsRmsDb.map((v: number, i: number) => (v <= -60 ? i + 1 : 0)).filter(Boolean), 'os mudos vão no JSON');
    assert.ok(Array.isArray(m.warnings));
    console.log(`    prévia de 2 voltas de 4 compassos a 140 BPM: ${m.barsRmsDb.length} compassos (${m.barsRmsDb.join(' · ')}), mudos: ${m.mutedBars.join(', ') || 'nenhum'}`);

    const appPid = JSON.parse(readFileSync(controlFile, 'utf8')).pid as number;
    const serverPid = transport.pid!;
    const t0 = Date.now();
    await client.close();
    assert.ok(await gone(serverPid, 10000));
    assert.ok(await gone(appPid, 10000));
    assert.ok(!existsSync(controlFile), 'o app fechou pelo app.quit e apagou o arquivo de controle');
    assert.ok(Date.now() - t0 < 5000, `fechou em ${Date.now() - t0} ms, sem esperar o tempo de fechar à força`);
  } finally {
    await client.close().catch(() => {});
    rmSync(dir, { recursive: true, force: true });
  }
});

// A confirmação sem janela (fatia 4.5): o app devolve um ticket, o servidor pergunta à pessoa pelo cliente
// (input_required com elicitação na 2026-07-28; o shim do SDK faz o elicitation/create nos clientes antigos)
for (const era of ['2026-07-28', '2025-11-25'] as const) {
  test(`confirmação sem janela, cliente ${era}: o sim roda, o não fica, e o cliente sem elicitação ouve o caminho da janela`,
       { skip, timeout: 180000 }, async () => {
    const asked: string[] = [];
    let reply: object = { action: 'accept', content: { confirmar: true } };
    const { client, dir } = await start(era === '2026-07-28' ? { mode: { pin: era } } : undefined, (message) => { asked.push(message); return reply; });

    try {
      const favorites = async () => (JSON.parse(textOf(await client.callTool({ name: 'command_run', arguments: { id: 'favorites.list' } }))).value.favorites as any[]);
      assert.equal((await favorites()).length, 0, 'começa sem favoritos (pasta de dados do teste)');

      const yes = await client.callTool({ name: 'command_run', arguments: { id: 'favorites.set', params: { kind: 'effect', id: 'reverb' } } });
      assert.ok(!yes.isError, textOf(yes));
      assert.equal(asked.length, 1, 'a pessoa foi perguntada uma vez');
      assert.match(asked[0], /A IA quer: .*\(favorites\.set\)\. Grava uma preferência sua/);
      assert.deepEqual((await favorites()).map((f) => f.id), ['reverb'], 'com o sim, o favorito entrou');

      reply = { action: 'decline' };
      const no = await client.callTool({ name: 'command_run', arguments: { id: 'favorites.set', params: { kind: 'effect', id: 'delay' } } });
      assert.ok(no.isError);
      assert.match(textOf(no), /não confirmou/);
      assert.deepEqual((await favorites()).map((f) => f.id), ['reverb'], 'com o não, nada muda');
    } finally {
      await client.close().catch(() => {});
      rmSync(dir, { recursive: true, force: true });
    }

    // o mesmo pedido num cliente que não declarou elicitation
    const plain = await start(era === '2026-07-28' ? { mode: { pin: era } } : undefined);

    try {
      const r = await plain.client.callTool({ name: 'command_run', arguments: { id: 'favorites.set', params: { kind: 'effect', id: 'reverb' } } });
      assert.ok(r.isError);
      assert.match(textOf(r), /Este cliente de IA não pergunta/);
    } finally {
      await plain.client.close().catch(() => {});
      rmSync(plain.dir, { recursive: true, force: true });
    }
  });
}
