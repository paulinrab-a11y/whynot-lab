// As tools com uma ponte de mentira: o que vai para o app e o que volta para a IA.

import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { test } from 'node:test';

import { createRequestStateCodec, isInputRequiredResult } from '@modelcontextprotocol/server';

import { BridgeError, type BridgeLike, type CommandInfo } from '../src/bridge.js';
import { commandBatch, commandList, commandRun, describePreview, getProject, renderPreview, screenshot, stableJson, timeoutFor,
         type ConfirmRound, type ConfirmState } from '../src/tools.js';

const COMMANDS: CommandInfo[] = [
  { id: 'transport.setBpm', title: 'Mudar BPM', domain: 'transport', risk: 'free', undoable: true,
    params: { type: 'object', properties: { bpm: { type: 'number', description: 'Andamento' } }, required: ['bpm'] } },
  { id: 'transport.play', title: 'Tocar', domain: 'transport', risk: 'free', undoable: false, params: { type: 'object', properties: {}, required: [] } },
  { id: 'channel.addPlugin', title: 'Adicionar instrumento', domain: 'channel', risk: 'confirm', why: 'abre um VST3', undoable: true,
    params: { type: 'object', properties: { plugin: { type: 'string' } }, required: ['plugin'] } },
  { id: 'audio.setDevice', title: 'Escolher placa de som', domain: 'audio', risk: 'blocked', why: 'muda o computador', undoable: false,
    params: { type: 'object', properties: {}, required: [] } },
  { id: 'project.exportWav', title: 'Exportar WAV', domain: 'project', risk: 'free', undoable: false,
    confirmsWhen: 'por cima de um arquivo que já existe; com um caminho fora das pastas permitidas',
    params: { type: 'object', properties: { path: { type: 'string' } }, required: ['path'] } },
];

class FakeBridge implements BridgeLike {
  calls: { method: string; params: any; timeoutMs?: number }[] = [];
  replies: Record<string, any> = {};
  failWith: Error | null = null;

  async commands() { return COMMANDS; }

  async call(method: string, params: any = {}, timeoutMs?: number) {
    if (this.failWith)
      throw this.failWith;

    this.calls.push({ method, params, timeoutMs });
    const reply = this.replies[method];
    return typeof reply === 'function' ? reply(params) : reply;
  }
}

const textOf = (r: any) => r.content[0].text as string;

test('command_list sem filtro: compacto, por domínio, com as marcas de risco', async () => {
  const r = await commandList(new FakeBridge(), {});
  assert.ok(!r.isError);
  assert.match(textOf(r), /^5 comandos em 4 domínios/);
  assert.match(textOf(r), /transport: setBpm, play/);
  assert.match(textOf(r), /channel: addPlugin \[confirma\]/);
  assert.match(textOf(r), /audio: setDevice \[bloqueado\]/);
  assert.match(textOf(r), /project: exportWav \[às vezes confirma\]/);

  const detail = await commandList(new FakeBridge(), { ids: ['project.exportWav'] });
  assert.match((detail.structuredContent as any).commands[0].confirmsWhen, /por cima de um arquivo/);
});

test('command_list com domínio ou busca traz o schema; filtro sem resultado diz os domínios', async () => {
  const byDomain = await commandList(new FakeBridge(), { domain: 'transport' });
  assert.deepEqual((byDomain.structuredContent as any).commands.map((c: any) => c.id), ['transport.setBpm', 'transport.play']);
  assert.equal((byDomain.structuredContent as any).commands[0].params.required[0], 'bpm');

  const byQuery = await commandList(new FakeBridge(), { query: 'placa' });
  assert.deepEqual((byQuery.structuredContent as any).commands.map((c: any) => c.id), ['audio.setDevice']);
  assert.equal((byQuery.structuredContent as any).commands[0].why, 'muda o computador');

  const none = await commandList(new FakeBridge(), { domain: 'nada' });
  assert.ok(none.isError);
  assert.match(textOf(none), /Os domínios são: transport, channel, audio, project/);
});

test('command_run: valida antes de mandar, e o que o app devolve vai para a IA', async () => {
  const bridge = new FakeBridge();
  bridge.replies['command.run'] = { ok: true, value: null, undo: 'IA: Mudar BPM', dirty: true };

  const wrong = await commandRun(bridge, { id: 'transport.setBpm', params: { bpm: 'rápido' } });
  assert.ok(wrong.isError);
  assert.match(textOf(wrong), /"bpm" precisa ser number/);
  assert.equal(bridge.calls.length, 0, 'parâmetro errado não chega ao app');

  const ok = await commandRun(bridge, { id: 'transport.setBpm', params: { bpm: 150, extra: 1 } });
  assert.ok(!ok.isError);
  assert.deepEqual(ok.structuredContent, { value: null, undo: 'IA: Mudar BPM', dirty: true });
  assert.match(textOf(ok), /Aviso: "extra" não é parâmetro/);
  assert.deepEqual(bridge.calls[0], { method: 'command.run', params: { id: 'transport.setBpm', params: { bpm: 150, extra: 1 } }, timeoutMs: 30000 });
});

test('command_run: desconhecido sugere parecidos; confirmação e bloqueio viram isError com o motivo', async () => {
  const bridge = new FakeBridge();

  const unknown = await commandRun(bridge, { id: 'transport.setTempo' });
  assert.ok(unknown.isError);
  assert.match(textOf(unknown), /Comando desconhecido: transport\.setTempo\. Parecidos: transport\.setBpm, transport\.play/);

  bridge.replies['command.run'] = { ok: false, error: 'Este comando precisa da sua confirmação: Abre um VST3.', needsConfirmation: { summary: 'Abre um VST3.' } };
  const confirm = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } });
  assert.ok(confirm.isError);
  assert.match(textOf(confirm), /Precisa da confirmação da pessoa: Abre um VST3\. .*pela tela/);

  bridge.replies['command.run'] = { ok: false, error: 'Este comando é só para você, pela tela: muda o computador.', refused: true };
  const blocked = await commandRun(bridge, { id: 'audio.setDevice' }) as any;
  assert.ok(blocked.isError);
  assert.match(textOf(blocked), /só para você, pela tela/);
});

//==============================================================================
// A confirmação sem janela (fatia 4.5): o app devolve um ticket, e o command_run pergunta pelo cliente de IA
const ticketReply = { ok: false, error: 'Precisa da confirmação da pessoa: Abre um VST3.',
                      needsConfirmation: { summary: 'Abre um VST3.', title: 'Adicionar instrumento', ticket: 'T'.repeat(43), expiresInSeconds: 120 } };

function round(over: Partial<ConfirmRound> = {}): ConfirmRound & { minted: ConfirmState[] } {
  const minted: ConfirmState[] = [];
  return { canElicit: true, answer: { kind: 'missing' }, mint: async (s) => { minted.push(s); return 'estado'; }, minted, ...over };
}

test('confirmação sem janela: o ticket vira a pergunta ao cliente (elicitação), com o estado assinado', async () => {
  const bridge = new FakeBridge();
  bridge.replies['command.run'] = ticketReply;
  const r = round();

  const asked = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } }, r);
  assert.ok(isInputRequiredResult(asked), 'devolve input_required');
  const request = (asked as any).inputRequests.confirmar;
  assert.equal(request.method, 'elicitation/create');
  assert.match(request.params.message, /A IA quer: Adicionar instrumento \(channel\.addPlugin\)\. Abre um VST3\. Permitir uma vez\?/);
  assert.equal(request.params.requestedSchema.properties.confirmar.type, 'boolean');
  assert.equal((asked as any).requestState, 'estado');
  assert.deepEqual(r.minted, [{ ticket: 'T'.repeat(43), id: 'channel.addPlugin', params: '{"plugin":"Surge XT"}' }]);
  assert.equal(bridge.calls.length, 1, 'ainda não confirmou: só o pedido que voltou com o ticket');
});

test('confirmação sem janela: com o sim da pessoa, o pedido volta ao app com o ticket; recusa e outro pedido não rodam', async () => {
  const bridge = new FakeBridge();
  bridge.replies['command.run'] = (p: any) => (p.confirm ? { ok: true, value: { channel: 'ch_9' }, undo: 'IA: Adicionar instrumento', dirty: true } : ticketReply);
  const state: ConfirmState = { ticket: 'T'.repeat(43), id: 'channel.addPlugin', params: '{"plugin":"Surge XT"}' };

  const yes = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } },
                               round({ state, answer: { kind: 'elicit', action: 'accept', content: { confirmar: true } } })) as any;
  assert.ok(!yes.isError, textOf(yes));
  assert.deepEqual(bridge.calls.at(-1)!.params, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' }, confirm: 'T'.repeat(43) });
  assert.equal(yes.structuredContent.undo, 'IA: Adicionar instrumento');

  const calls = bridge.calls.length;

  for (const answer of [{ kind: 'elicit', action: 'decline' }, { kind: 'elicit', action: 'cancel' },
                        { kind: 'elicit', action: 'accept', content: { confirmar: false } }, { kind: 'missing' }] as const) {
    const no = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } }, round({ state, answer: answer as any })) as any;
    assert.ok(no.isError);
    assert.match(textOf(no), /não confirmou.*Não tente de novo pelo mesmo caminho/);
  }

  const other = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Outro' } },
                                 round({ state, answer: { kind: 'elicit', action: 'accept', content: { confirmar: true } } })) as any;
  assert.ok(other.isError);
  assert.match(textOf(other), /era de outro pedido/);
  assert.equal(bridge.calls.length, calls, 'sem sim, ou com outros parâmetros, nada vai ao app');
});

test('confirmação sem janela: cliente que não pergunta (sem elicitation) ouve o caminho da janela', async () => {
  const bridge = new FakeBridge();
  bridge.replies['command.run'] = ticketReply;
  const r = await commandRun(bridge, { id: 'channel.addPlugin', params: { plugin: 'Surge XT' } }, round({ canElicit: false })) as any;
  assert.ok(r.isError);
  assert.match(textOf(r), /Este cliente de IA não pergunta.*--control/);
});

//==============================================================================
// Ver e ouvir (fatia 4.6)
const measures = { ok: true, path: 'C:\\IA\\previa.wav', what: 'pattern', loops: 2, bpm: 140, seconds: 3.43, sampleRate: 48000, channels: 2,
                   peakDb: -3.2, truePeakDb: -3.0, rmsDb: -14.1, lufs: -11.8, clipped: 0, silenceStart: 0, silenceEnd: 0.35,
                   barSeconds: 1.71, barsRmsDb: [-13.9, -100, -70.2], barsCut: false };

test('render_preview: as medidas em texto curto, com os compassos mudos, e o JSON junto', async () => {
  const bridge = new FakeBridge();
  bridge.replies['render.preview'] = measures;
  const r = await renderPreview(bridge, { what: 'pattern', loops: 2 });
  assert.ok(!r.isError);
  assert.deepEqual(bridge.calls[0], { method: 'render.preview', params: { what: 'pattern', loops: 2 }, timeoutMs: 300000 });
  assert.match(textOf(r), /^Prévia do padrão \(2 voltas\): 3,43 s, 48 kHz, estéreo, 140 BPM\./);
  assert.match(textOf(r), /Pico -3,2 dBFS \(true peak -3,0 dBTP\) · RMS -14,1 dBFS · -11,8 LUFS integrado\./);
  assert.match(textOf(r), /Nada cortou\./);
  assert.match(textOf(r), /1: -13,9 · 2: mudo · 3: -70,2/);
  assert.match(textOf(r), /Compassos mudos \(abaixo de -60 dBFS\): 2, 3\./);
  assert.match(textOf(r), /não é ouvir de verdade/);
  assert.equal((r.structuredContent as any).lufs, -11.8);

  // o Claude Code mostra ao modelo só o structuredContent: os avisos do texto vão nele também (teste 6.5)
  assert.deepEqual((r.structuredContent as any).mutedBars, [2, 3]);
  assert.deepEqual((r.structuredContent as any).warnings, ['Compassos mudos (abaixo de -60 dBFS): 2, 3.']);
  assert.equal((r.structuredContent as any).ok, undefined);

  bridge.replies['render.preview'] = { ...measures, clipped: 12 };
  const clipped = await renderPreview(bridge, {});
  assert.match(textOf(clipped), /CORTOU: 12 amostras em 0 dBFS/);
  assert.doesNotMatch(textOf(clipped), /Nada cortou/);
  assert.deepEqual((clipped.structuredContent as any).warnings, ['CORTOU: 12 amostras em 0 dBFS.', 'Compassos mudos (abaixo de -60 dBFS): 2, 3.']);

  bridge.replies['render.preview'] = { ok: false, error: 'what é "pattern" ou "song".' };
  const bad = await renderPreview(bridge, {});
  assert.ok(bad.isError);
});

test('render_preview: 8 compassos sem o fantasma não têm aviso; o parcial, a prévia muda e o corte da lista avisam', async () => {
  const bridge = new FakeBridge();
  const eight = { ...measures, loops: 2, seconds: 13.71, barSeconds: 1.71, lastBarPartial: false,
                  barsRmsDb: [-13.3, -13.4, -13.3, -13.5, -13.3, -13.4, -13.3, -14.5] };
  bridge.replies['render.preview'] = eight;
  const r = await renderPreview(bridge, {});
  assert.doesNotMatch(textOf(r), /mudo|parcial/);
  assert.match(textOf(r), /8: -14,5\nArquivo:/);
  assert.deepEqual((r.structuredContent as any).mutedBars, []);
  assert.deepEqual((r.structuredContent as any).warnings, []);

  assert.match(describePreview({ ...eight, lastBarPartial: true, barsRmsDb: [...eight.barsRmsDb, -13.2] }), /8: -14,5 · 9 \(parcial\): -13,2/);

  const silent = { ...eight, peakDb: -100, barsRmsDb: [-100, -100] };
  bridge.replies['render.preview'] = silent;
  const quiet = await renderPreview(bridge, {});
  assert.deepEqual((quiet.structuredContent as any).warnings,
                   ['A prévia saiu muda: o pico ficou abaixo de -60 dBFS.', 'Compassos mudos (abaixo de -60 dBFS): 1, 2.']);

  bridge.replies['render.preview'] = { ...eight, barsCut: true };
  assert.deepEqual(((await renderPreview(bridge, {})).structuredContent as any).warnings, ['Só os 8 primeiros compassos têm RMS.']);
});

test('screenshot: a imagem em PNG vai para o modelo; acima de 1 MB, pede menor', async () => {
  const bridge = new FakeBridge();
  bridge.replies['ui.screenshot'] = (p: any) => ({ ok: true, path: `tela-${p.width}.png`, width: p.width, height: Math.round(p.width * 0.625) });
  const sizes: Record<string, number> = { 'tela-1280.png': 1_500_000, 'tela-896.png': 700_000 };
  const r = await screenshot(bridge, { view: 'pianoRoll' }, (path) => Buffer.alloc(sizes[path] ?? 10, 1));

  assert.ok(!r.isError);
  assert.deepEqual(bridge.calls.map((c) => c.params.width), [1280, 896], 'a de 1,5 MB volta a 70% da largura');
  assert.equal(bridge.calls[0].params.view, 'pianoRoll');
  assert.equal(r.content[0].type, 'image');
  assert.equal((r.content[0] as any).mimeType, 'image/png');
  assert.equal(Buffer.from((r.content[0] as any).data, 'base64').length, 700_000);
  assert.match((r.content[1] as any).text, /Tela "pianoRoll": 896x560 px, 684 KB/);

  bridge.replies['ui.screenshot'] = { ok: false, error: 'Tela desconhecida: x.' };
  const bad = await screenshot(bridge, {}, () => Buffer.alloc(1));
  assert.ok(bad.isError);
  assert.match(textOf(bad), /Tela desconhecida/);
});

test('screenshot { channel }: escolhe o canal do piano roll; sem view, é o piano roll dele', async () => {
  const bridge = new FakeBridge();
  bridge.replies['ui.screenshot'] = (p: any) => ({ ok: true, path: 'tela.png', width: p.width, height: 800, view: p.view ?? '',
                                                   channel: p.channel ?? 'ch_1', channelName: p.channel === 'ch_6' ? '808' : 'Kick' });
  const r = await screenshot(bridge, { channel: 'ch_6' }, () => Buffer.alloc(10, 1));

  assert.ok(!r.isError);
  assert.deepEqual(bridge.calls[0].params, { view: 'pianoRoll', channel: 'ch_6', width: 1280 });
  assert.match((r.content[1] as any).text, /^Tela "pianoRoll", canal "808" \(ch_6\): 1280x800 px/);

  const rack = await screenshot(bridge, { view: 'channelRack' }, () => Buffer.alloc(10, 1));
  assert.deepEqual(bridge.calls[1].params, { view: 'channelRack', width: 1280 });
  assert.match((rack.content[1] as any).text, /^Tela "channelRack": 1280x800 px/, 'fora do piano roll, o texto não fala do canal');

  bridge.replies['ui.screenshot'] = { ok: false, error: 'Canal desconhecido: ch_9. Os canais com piano roll: Kick (ch_1), 808 (ch_6).' };
  const bad = await screenshot(bridge, { channel: 'ch_9' }, () => Buffer.alloc(1));
  assert.ok(bad.isError);
  assert.match(textOf(bad), /Canal desconhecido: ch_9\. Os canais com piano roll: Kick \(ch_1\)/);
});

test('stableJson: as mesmas chaves em outra ordem dão o mesmo texto', () => {
  assert.equal(stableJson({ b: 1, a: { d: [1, { z: 2, y: 3 }], c: 'x' } }), '{"a":{"c":"x","d":[1,{"y":3,"z":2}]},"b":1}');
  assert.equal(stableJson({ path: 'C:\\a.wav', loops: 1 }), stableJson({ loops: 1, path: 'C:\\a.wav' }));
});

test('command_run: export tem tempo limite de 5 min; o resto, 30 s', async () => {
  assert.equal(timeoutFor('project.exportWav'), 300000);
  assert.equal(timeoutFor('plugins.scan'), 300000);
  assert.equal(timeoutFor('transport.setBpm'), 30000);

  const bridge = new FakeBridge();
  bridge.replies['command.run'] = { ok: true, value: { path: 'C:/x.wav' } };
  await commandRun(bridge, { id: 'project.exportWav', params: { path: 'C:/x.wav' } });
  assert.equal(bridge.calls[0].timeoutMs, 300000);
});

test('command_batch: confere cada passo antes; erro diz qual passo', async () => {
  const bridge = new FakeBridge();
  bridge.replies['command.batch'] = { ok: true, value: { results: [null, null] }, undo: 'IA: Dois', dirty: true };

  const bad = await commandBatch(bridge, { title: 'Dois', steps: [{ id: 'transport.setBpm', params: { bpm: 100 } }, { id: 'transport.setBpm', params: {} }] });
  assert.ok(bad.isError);
  assert.match(textOf(bad), /^Passo 2 \(transport\.setBpm\): falta "bpm"/);

  const unknown = await commandBatch(bridge, { title: 'X', steps: [{ id: 'nada.disso' }] });
  assert.match(textOf(unknown), /^Passo 1: Comando desconhecido/);
  assert.equal(bridge.calls.length, 0);

  const good = await commandBatch(bridge, { title: 'Dois', steps: [{ id: 'transport.setBpm', params: { bpm: 100 } }, { id: 'transport.play' }] });
  assert.ok(!good.isError);
  assert.deepEqual(bridge.calls[0].params, { title: 'Dois', steps: [{ id: 'transport.setBpm', params: { bpm: 100 } }, { id: 'transport.play', params: {} }] });

  const tooMany = await commandBatch(bridge, { title: 'X', steps: Array.from({ length: 201 }, () => ({ id: 'transport.play' })) });
  assert.match(textOf(tooMany), /mais de 200 passos/);
});

test('get_project: resumo junta o projeto e o estado; completo grande pede filtro', async () => {
  const bridge = new FakeBridge();
  bridge.replies['project.inspect'] = (params: any) => (params.sections?.length
    ? { format: 'whynot-lab-project', project: { name: 'Beat', bpm: 150 } }
    : { project: { name: 'Beat', bpm: 150 }, channels: [], patterns: [], playlist: { tracks: [], items: [] }, mixer: { tracks: [] }, big: 'x'.repeat(200 * 1024) });
  bridge.replies['app.status'] = { playing: true, dirty: false, file: 'C:/Beat.wnl', undo: '', redo: '' };

  const summary = await getProject(bridge, {});
  assert.match(textOf(summary), /^Projeto "Beat" · 150 BPM/);
  assert.match(textOf(summary), /tocando · sem alterações desde o último salvar · arquivo C:\/Beat\.wnl/);

  const full = await getProject(bridge, { view: 'completo' });
  assert.ok(!full.isError);
  assert.match(textOf(full), /acima de 100 KB\. Peça uma parte.*big 20\d KB/s);

  const part = await getProject(bridge, { view: 'completo', sections: ['project'] });
  assert.deepEqual(JSON.parse(textOf(part)).project, { name: 'Beat', bpm: 150 });
});

test('erro da ponte (app fechado) vira isError com a explicação, sem derrubar o servidor', async () => {
  const { registerTools } = await import('../src/tools.js');
  const handlers: Record<string, (args: any) => Promise<any>> = {};
  const fakeServer = { registerTool: (name: string, _config: unknown, cb: (args: any) => Promise<any>) => { handlers[name] = cb; } };
  const bridge = new FakeBridge();
  registerTools(fakeServer as any, bridge, createRequestStateCodec<ConfirmState>({ key: randomBytes(32) }));

  assert.deepEqual(Object.keys(handlers), ['command_list', 'command_run', 'command_batch', 'render_preview', 'screenshot', 'get_project'],
                   'as tools em ordem fixa');

  bridge.failWith = new BridgeError('A WHYNOT não está aberta com --control 47830.');
  bridge.commands = async () => { throw bridge.failWith!; };
  const r = await handlers.command_run({ id: 'transport.play' });
  assert.ok(r.isError);
  assert.match(r.content[0].text, /não está aberta com --control 47830/);
});
