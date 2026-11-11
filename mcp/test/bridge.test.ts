// A ponte (bridge.ts) sem o app: as contas da queda, e os caminhos de erro contra uma ponte de mentira, que é um
// servidor WebSocket mínimo feito aqui com o node:http (aperto de mão da RFC 6455 e quadros de texto curtos).
// ESCRITO SEM NODE NA MÁQUINA (10/11/2026): nunca rodou. Se falhar no CI, a intenção de cada teste está no nome e
// nos comentários; o mais provável de precisar de ajuste é a ponte de mentira (fakeApp), não a ponte de verdade.

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import type { AddressInfo } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import type { Duplex } from 'node:stream';
import { test } from 'node:test';

import { Bridge, BridgeError, MAX_REQUEST_BYTES, crashNotice, projectFileAfter } from '../src/bridge.js';

//==============================================================================
// As contas da queda do app sem janela (achado 1): funções puras

test('projectFileAfter: o caminho do projeto vem do hello, do app.status e dos comandos de projeto que deram certo', () => {
  assert.equal(projectFileAfter(null, 'hello', {}, { project: { file: 'C:\\a.wnl' } }), 'C:\\a.wnl');
  assert.equal(projectFileAfter('C:\\a.wnl', 'app.status', {}, { file: '' }), '', 'projeto nunca salvo: vazio');
  assert.equal(projectFileAfter('C:\\a.wnl', 'app.status', {}, {}), 'C:\\a.wnl', 'resposta sem file não apaga o que se sabia');

  const run = (id: string, params: object, result: object) => projectFileAfter('C:\\a.wnl', 'command.run', { id, params }, result);
  assert.equal(run('project.open', { path: 'C:\\b.wnl' }, { ok: true }), 'C:\\b.wnl');
  assert.equal(run('project.save', { path: 'C:\\c.wnl' }, { ok: true }), 'C:\\c.wnl');
  assert.equal(run('project.save', {}, { ok: true }), 'C:\\a.wnl', 'salvar sem path grava no arquivo atual');
  assert.equal(run('project.saveNewVersion', {}, { ok: true, value: { path: 'C:\\a 2.wnl' } }), 'C:\\a 2.wnl');
  assert.equal(run('project.new', {}, { ok: true }), '');
  assert.equal(run('project.open', { path: 'C:\\b.wnl' }, { ok: false, error: 'não abriu' }), 'C:\\a.wnl', 'comando que falhou não muda nada');
  assert.equal(run('transport.setBpm', { bpm: 150 }, { ok: true }), 'C:\\a.wnl');
  assert.equal(projectFileAfter('C:\\a.wnl', 'command.batch', {}, { ok: true }), 'C:\\a.wnl');
});

test('crashNotice: diz que fechou, que o projeto agora é vazio, que o pedido não rodou, e o caminho do último projeto', () => {
  const known = crashNotice('3221225477', 'C:\\Beats\\trap.wnl');
  assert.match(known, /^A WHYNOT LAB fechou inesperadamente \(saiu com o código 3221225477\)\./);
  assert.match(known, /projeto vazio/);
  assert.match(known, /este pedido NÃO rodou/);
  assert.match(known, /C:\\Beats\\trap\.wnl/);
  assert.match(known, /project\.open/);
  assert.match(known, /"C:\\\\Beats\\\\trap\.wnl"/, 'no exemplo do comando, o caminho vai como texto JSON (barras dobradas)');

  for (const unknown of [crashNotice('null', null), crashNotice(null, '')]) {
    assert.match(unknown, /^A WHYNOT LAB fechou inesperadamente\. /, 'sem código de saída útil, não inventa um');
    assert.match(unknown, /Não sei de arquivo salvo/);
    assert.match(unknown, /project\.open <caminho>/);
  }
});

//==============================================================================
// A ponte de mentira: aceita o WebSocket, entrega cada pedido JSON ao teste e manda de volta o que ele devolver
// (um objeto vira JSON; um texto vai como está, para testar resposta malformada; undefined = não responde).

type Answer = (request: any, socket: Duplex) => unknown;

function frame(text: string): Buffer {
  const payload = Buffer.from(text, 'utf8');
  assert.ok(payload.length < 65536, 'a ponte de mentira só manda quadros curtos');
  const header = payload.length < 126 ? Buffer.from([0x81, payload.length]) : Buffer.from([0x81, 126, payload.length >> 8, payload.length & 0xff]);
  return Buffer.concat([header, payload]);
}

async function fakeApp(answer: Answer) {
  const sockets = new Set<Duplex>();
  const requests: any[] = [];
  const server = createServer();

  server.on('upgrade', (request, socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
    socket.on('error', () => {});

    const accept = createHash('sha1').update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);

    let buffer: Buffer = Buffer.alloc(0);

    socket.on('data', (chunk: Buffer) => {
      buffer = Buffer.concat([buffer, chunk]);

      // os quadros do cliente vêm mascarados: 2 bytes, o tamanho estendido (126 = 16 bits), 4 de máscara e o conteúdo
      for (;;) {
        if (buffer.length < 2)
          return;

        let length = buffer[1] & 0x7f;
        let offset = 2;

        if (length === 126) {
          if (buffer.length < 4)
            return;

          length = buffer.readUInt16BE(2);
          offset = 4;
        }

        if (buffer.length < offset + 4 + length)
          return;

        const opcode = buffer[0] & 0x0f;
        const mask = buffer.subarray(offset, offset + 4);
        const payload = Buffer.from(buffer.subarray(offset + 4, offset + 4 + length));

        for (let i = 0; i < payload.length; ++i)
          payload[i] ^= mask[i % 4];

        buffer = buffer.subarray(offset + 4 + length);

        if (opcode === 8) {   // o cliente fechou
          socket.end();
          return;
        }

        if (opcode !== 1)
          continue;

        const message = JSON.parse(payload.toString('utf8'));
        requests.push(message);

        // o hello a ponte de mentira responde sozinha; o resto é do teste
        const reply = message.method === 'hello'
          ? { jsonrpc: '2.0', id: message.id, result: { app: 'WHYNOT LAB', version: 'teste', protocol: 1, headless: false, project: { file: '' } } }
          : answer(message, socket);

        if (reply !== undefined)
          socket.write(frame(typeof reply === 'string' ? reply : JSON.stringify(reply)));
      }
    });
  });

  await new Promise<void>((done) => server.listen(0, '127.0.0.1', done));
  const port = (server.address() as AddressInfo).port;

  // o modo --attach lê %APPDATA%\WHYNOT LAB\control\<porta>.json: o teste aponta o APPDATA para uma pasta dele
  const appData = mkdtempSync(join(tmpdir(), 'whynot-ponte-teste-'));
  const controlFolder = join(appData, 'WHYNOT LAB', 'control');
  mkdirSync(controlFolder, { recursive: true });
  writeFileSync(join(controlFolder, `${port}.json`), JSON.stringify({ port, token: 'token-do-teste', pid: process.pid }));

  const before = process.env.APPDATA;
  process.env.APPDATA = appData;
  const bridge = new Bridge({ mode: 'attach', port });

  const stop = async () => {
    await bridge.close();

    if (before === undefined)
      delete process.env.APPDATA;
    else
      process.env.APPDATA = before;

    for (const socket of sockets)
      socket.destroy();

    await new Promise((done) => server.close(done));
    rmSync(appData, { recursive: true, force: true });
  };

  return { bridge, requests, sockets, controlFolder, port, stop };
}

const methods = (requests: any[]) => requests.map((r) => r.method);

/** A promessa tem de falhar com um BridgeError (é o que vira isError para a IA) cuja mensagem bate com o padrão. */
async function rejectsWith(promise: Promise<unknown>, pattern: RegExp) {
  await assert.rejects(promise, (error: unknown) => {
    assert.ok(error instanceof BridgeError, `era para ser BridgeError, veio ${String(error)}`);
    assert.match((error as Error).message, pattern);
    return true;
  });
}

test('ponte: liga com o hello, devolve o result, e o erro do app ("Ocupado", IA desligada) vira BridgeError com a frase dele', async () => {
  const app = await fakeApp((r) => (r.method === 'app.status'
    ? { jsonrpc: '2.0', id: r.id, result: { playing: false, file: 'C:\\a.wnl' } }
    : { jsonrpc: '2.0', id: r.id, error: { code: -32000, message: 'Ocupado: pedidos demais de uma vez; espere as respostas.' } }));

  try {
    assert.deepEqual(await app.bridge.call('app.status'), { playing: false, file: 'C:\\a.wnl' });
    assert.deepEqual(methods(app.requests), ['hello', 'app.status'], 'o hello vai uma vez, antes do primeiro pedido');

    await rejectsWith(app.bridge.call('command.run', { id: 'transport.play', params: {} }),
                      /^Ocupado: pedidos demais de uma vez; espere as respostas\. \(erro -32000 da ponte\)$/);
  } finally {
    await app.stop();
  }
});

test('ponte: resposta malformada do app (JSON que não é objeto, lixo, objeto sem result) não derruba o servidor', async () => {
  let mode = 'lixo';
  const app = await fakeApp((r) => {
    if (mode === 'sem-result')
      return { jsonrpc: '2.0', id: r.id };

    if (mode === 'lista-errada')
      return { jsonrpc: '2.0', id: r.id, result: { commands: 'não é lista' } };

    return undefined;
  });

  try {
    // antes do pedido, o app manda "null" e um texto que não é JSON: os dois são ignorados (antes, o null derrubava o processo)
    await app.bridge.call('hello', {});   // só para ligar
    for (const socket of app.sockets) {
      socket.write(frame('null'));
      socket.write(frame('isto não é JSON'));
      socket.write(frame('42'));
    }

    // sem resposta que sirva, o pedido acaba no tempo limite, com a frase de sempre
    await rejectsWith(app.bridge.call('app.status', {}, 300), /não respondeu em 0 s\. O comando pode ter rodado: confira com get_project/);

    mode = 'sem-result';
    await rejectsWith(app.bridge.call('app.status'), /respondeu fora do formato/);

    mode = 'lista-errada';
    await rejectsWith(app.bridge.commands(), /não devolveu a lista de comandos/);
  } finally {
    await app.stop();
  }
});

test('ponte: pedido acima de 1 MB é recusado aqui, sem ir ao app (que fecharia a conexão com 1009)', async () => {
  const app = await fakeApp((r) => ({ jsonrpc: '2.0', id: r.id, result: { ok: true } }));

  try {
    await rejectsWith(app.bridge.call('command.run', { id: 'channel.rename', params: { name: 'x'.repeat(MAX_REQUEST_BYTES) } }),
                      /Pedido grande demais para a ponte da WHYNOT: 10\d\d KB, e o teto é 1024 KB\. Nada rodou/);
    assert.deepEqual(methods(app.requests), ['hello'], 'o pedido gigante não saiu daqui');

    assert.deepEqual(await app.bridge.call('command.run', { id: 'transport.play', params: {} }), { ok: true }, 'e a ligação continua boa');
  } finally {
    await app.stop();
  }
});

test('ponte: ligação que cai no meio de um pedido avisa na hora, e a chamada seguinte liga de novo', async () => {
  let drop = true;
  const app = await fakeApp((r, socket) => {
    if (drop) {
      socket.destroy();   // o app some sem responder
      return undefined;
    }

    return { jsonrpc: '2.0', id: r.id, result: { ok: true } };
  });

  try {
    await rejectsWith(app.bridge.call('command.run', { id: 'transport.play', params: {} }), /fechou a conexão no meio do pedido/);

    drop = false;
    assert.deepEqual(await app.bridge.call('command.run', { id: 'transport.play', params: {} }), { ok: true });
    assert.deepEqual(methods(app.requests), ['hello', 'command.run', 'hello', 'command.run'], 'ligou de novo, com outro hello');
  } finally {
    await app.stop();
  }
});

test('ponte: arquivo de controle que falta, ilegível ou incompleto vira BridgeError em português', async () => {
  const app = await fakeApp(() => undefined);

  try {
    const file = join(app.controlFolder, `${app.port}.json`);

    writeFileSync(file, '{ "port": 47');   // gravado pela metade
    await rejectsWith(app.bridge.call('app.status'), /arquivo de controle da WHYNOT .* está ilegível ou incompleto/);

    writeFileSync(file, JSON.stringify({ port: app.port }));   // sem o token
    await rejectsWith(app.bridge.call('app.status'), /ilegível ou incompleto/);

    rmSync(file);
    await rejectsWith(app.bridge.call('app.status'), new RegExp(`não está aberta com --control ${app.port}`));
    assert.equal(app.requests.length, 0, 'nada disso chegou a ligar');
  } finally {
    await app.stop();
  }
});
