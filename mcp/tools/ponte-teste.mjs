// Teste da ponte de controle da WHYNOT (Fase 4.2) pelo lado do cliente, sem pacote nenhum: só o WebSocket do Node 24.
//
//   node mcp/tools/ponte-teste.mjs --exe "<pasta>\WHYNOT LAB.exe"   abre o app sem janela, testa e fecha com app.quit
//   node mcp/tools/ponte-teste.mjs --port 47830 [--write]           liga no app aberto com --control 47830; só lê,
//                                                                   a não ser com --write (que muda o projeto aberto)
//
// O código de saída é o número de falhas.

import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = process.argv.slice(2);
const option = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? '  ok' : '  FALHOU'}: ${what}`); if (!ok) failures++; };

let app, dir, controlFile;

if (option('--exe')) {
  dir = mkdtempSync(join(tmpdir(), 'wnl-ponte-'));
  controlFile = join(dir, 'control.json');
  app = spawn(option('--exe'), ['--headless', '--control', '0', '--control-file', controlFile,
                                '--data', join(dir, 'dados'), '--parent-pid', String(process.pid)], { stdio: 'ignore' });
} else {
  controlFile = join(process.env.APPDATA, 'WHYNOT LAB', 'control', `${option('--port') ?? 47830}.json`);
}

const opening = Date.now();
while (!existsSync(controlFile)) {
  if (Date.now() - opening > 60000) { console.log('  FALHOU: a ponte não abriu em 60 s'); process.exit(1); }
  await sleep(100);
}
console.log(`  (a ponte abriu em ${Date.now() - opening} ms)`);
const { port, token } = JSON.parse(readFileSync(controlFile, 'utf8'));

// conectar com o token errado não abre
const refused = await new Promise((done) => {
  const bad = new WebSocket(`ws://127.0.0.1:${port}/`, { headers: { Authorization: 'Bearer errado' } });
  bad.onopen = () => { bad.close(); done(false); };
  bad.onerror = () => done(true);
});
check(refused, 'com o token errado, o WebSocket não abre');

const ws = new WebSocket(`ws://127.0.0.1:${port}/`, { headers: { Authorization: `Bearer ${token}` } });
await new Promise((ok, fail) => { ws.onopen = ok; ws.onerror = () => fail(new Error('o WebSocket não abriu')); });

const waiting = new Map();
let nextId = 0;
ws.onmessage = (event) => { const reply = JSON.parse(event.data); waiting.get(reply.id)?.(reply); waiting.delete(reply.id); };
const call = (method, params) => new Promise((done) => {
  const id = ++nextId;
  waiting.set(id, done);
  ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
});
const run = async (id, params = {}) => (await call('command.run', { id, params })).result;

// sair com o fechamento do WebSocket ainda andando derruba o Node no Windows (assert do libuv): espera fechar
const finish = async () => {
  if (ws.readyState !== WebSocket.CLOSED) {
    await new Promise((done) => { ws.onclose = done; ws.close(); setTimeout(done, 2000); });
  }
  console.log(failures === 0 ? '\nTUDO OK' : `\n${failures} falha(s)`);
  process.exit(failures);
};

const hello = (await call('hello', { protocol: 1 })).result;
check(hello?.protocol === 1, `hello: ${hello?.app} ${hello?.version}, protocolo ${hello?.protocol}, esquema ${hello?.schemaVersion}, ${hello?.headless ? 'sem janela' : 'com janela'}`);

const commands = (await call('command.list')).result?.commands ?? [];
const count = (risk) => commands.filter((c) => c.risk === risk).length;
check(commands.length >= 126 && commands.every((c) => c.params && c.risk),
      `command.list: ${commands.length} comandos (${count('free')} livres, ${count('confirm')} pedem confirmação, ${count('blocked')} bloqueados)`);

if (!app && !args.includes('--write')) {
  const status = (await call('app.status')).result;
  check(status && typeof status.dirty === 'boolean', `app.status (só leitura): ${status?.headless ? 'sem janela' : 'com janela'}, alterado: ${status?.dirty}`);
  await finish();
}

const t0 = performance.now();
const bpm = await run('transport.setBpm', { bpm: 150 });
const ms = (performance.now() - t0).toFixed(1);
const project = (await call('project.inspect', { sections: ['project'] })).result;
check(bpm?.ok && project?.project?.bpm === 150 && bpm.undo?.startsWith('IA: '), `transport.setBpm 150 em ${ms} ms, desfazer "${bpm?.undo}"`);

const undo = await run('edit.undo');
check(undo?.ok && (await call('project.inspect', { sections: ['project'] })).result?.project?.bpm === 140, 'edit.undo volta a 140');

const batch = (await call('command.batch', { title: 'Teste em Node', steps: [
  { id: 'transport.setBpm', params: { bpm: 96 } },
  { id: 'project.rename', params: { name: 'Feito pela ponte' } }] })).result;
check(batch?.ok && batch.undo === 'IA: Teste em Node', `command.batch: 2 passos num desfazer só ("${batch?.undo}")`);

const blocked = await run('audio.setDevice', { type: 'ASIO' });
check(!blocked?.ok && blocked?.refused, `bloqueado: ${blocked?.error}`);

const status = (await call('app.status')).result;
check(status?.dirty === true && status?.aiUndo === true, 'app.status: alterado, o último passo é da IA');

if (app) {
  const exited = new Promise((done) => app.on('exit', (code) => done(code)));
  const quit = (await call('app.quit')).result;
  const code = await Promise.race([exited, sleep(15000).then(() => 'não saiu')]);
  check(quit?.ok && code === 0 && !existsSync(controlFile), `app.quit: o app sai com ${code} e apaga o arquivo de controle`);
  rmSync(dir, { recursive: true, force: true });
}

await finish();
