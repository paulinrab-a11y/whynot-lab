// O resumo do projeto, feito de dois .wnl de verdade que o autoteste do app gravou (test/fixtures).

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { noteName, summarizeProject } from '../src/summary.js';

const fixture = (name: string) => JSON.parse(readFileSync(new URL(`../../test/fixtures/${name}`, import.meta.url), 'utf8'));

test('nomes de nota: 60 é Dó4, 36 é Dó2, 70 é Lá#4', () => {
  assert.equal(noteName(60), 'Dó4');
  assert.equal(noteName(36), 'Dó2');
  assert.equal(noteName(70), 'Lá#4');
});

test('resumo do projeto com mixagem: cabeçalho, canais, padrões, playlist e mixer', () => {
  const project = fixture('demo-mixagem.wnl');
  const status = { playing: false, dirty: true, file: '', undo: 'IA: Mudar BPM', redo: '', headless: true };
  const s = summarizeProject(project, status);

  assert.match(s, /^Projeto "Demo 3\.2" · 140 BPM · 4\/4 · Dó menor · SONG/);
  assert.match(s, /parado · alterado, não salvo · nunca salvo · sem janela: não há autosave/);
  assert.match(s, /Desfazer: "IA: Mudar BPM"/);
  assert.match(s, /Canais \(6\):/);
  assert.match(s, /- ch_\w+ "Kick" · sampler \([^)]*\) → mx_\w+ "Kick"/);
  assert.match(s, /"808" · sampler \([^)]*mono, glide \d+ ms\)/);
  assert.match(s, /Padrões \(\d+\):/);
  assert.match(s, /\d+ passos/);
  assert.match(s, /Playlist: \d+ blocos em 12 raias, até o compasso \d+/);
  assert.match(s, /Mixer \(\d+ trilhas\):/);
  assert.match(s, /mx_\w+ "Kick" -6,0 dB · eq/);
  assert.match(s, /mx_master "Master"/);
  assert.ok(Buffer.byteLength(s) < 6 * 1024, `o resumo ficou curto (${Buffer.byteLength(s)} bytes)`);
});

test('resumo com canais do WHYNOT Synth mostra o preset', () => {
  const s = summarizeProject(fixture('demo-synth.wnl'));
  assert.match(s, /"808 Synth" · WHYNOT Synth \(preset [^)]+\)/);
  assert.match(s, /Canais \(9\):/);
  assert.doesNotMatch(s, /Desfazer:/, 'sem o estado do app, não inventa desfazer');
});

test('projeto vazio ou estranho não derruba o resumo', () => {
  assert.match(summarizeProject({}), /Projeto "\?"/);
  assert.match(summarizeProject({ project: { name: 'X', bpm: 90 }, channels: [{ id: 'ch_1', name: 'A', kind: 'audio' }] }), /canal de áudio/);
  assert.match(summarizeProject({ automation: [{ id: 'au_1', target: { kind: 'mixer', track: 'mx_master', param: 'volume' }, points: [{}, {}] }] }),
               /au_1: volume em Master \(2 pontos\)/);
});
