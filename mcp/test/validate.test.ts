// A conferência dos parâmetros pelo schema que o app publica.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import { validateParams } from '../src/validate.js';

const setBpm = { type: 'object', properties: { bpm: { type: 'number', description: 'Andamento entre 20 e 999' } }, required: ['bpm'] };
const addNotes = {
  type: 'object',
  properties: {
    channel: { type: 'string', description: 'Id do canal' },
    notes: { type: 'array', description: 'Lista de notas' },
    unit: { type: 'string', description: 'ticks, steps, beats ou bars' },
    steps: { type: 'integer', description: 'Passos' },
    mono: { type: 'boolean', description: 'Mono' },
    target: { type: 'object', description: 'Alvo' },
  },
  required: ['channel', 'notes'],
};

test('parâmetros certos passam, sem aviso', () => {
  assert.deepEqual(validateParams({ bpm: 150 }, setBpm), { errors: [], warnings: [] });
  assert.deepEqual(validateParams({ bpm: 92.5 }, setBpm), { errors: [], warnings: [] });
  assert.deepEqual(validateParams({ channel: 'ch_1', notes: [], steps: 16, mono: true, target: {} }, addNotes).errors, []);
});

test('falta o obrigatório: o erro diz o nome, o tipo e a descrição', () => {
  const v = validateParams({}, setBpm);
  assert.equal(v.errors.length, 1);
  assert.match(v.errors[0], /falta "bpm" \(number\): Andamento/);
  assert.equal(validateParams(undefined, setBpm).errors.length, 1);
});

test('tipo errado: número em texto, inteiro com casas, lista que não é lista', () => {
  assert.match(validateParams({ bpm: '150' }, setBpm).errors[0], /"bpm" precisa ser number, veio string/);
  assert.match(validateParams({ channel: 'ch_1', notes: [], steps: 1.5 }, addNotes).errors[0], /"steps" precisa ser integer, veio number/);
  assert.match(validateParams({ channel: 'ch_1', notes: {} }, addNotes).errors[0], /"notes" precisa ser array, veio object/);
  assert.match(validateParams({ channel: 'ch_1', notes: [], target: [] }, addNotes).errors[0], /"target" precisa ser object, veio array/);
  assert.match(validateParams({ channel: null, notes: [] }, addNotes).errors[0], /veio null/);
});

test('parâmetro que o comando não tem vira aviso, não erro', () => {
  const v = validateParams({ bpm: 150, tempo: 'rápido' }, setBpm);
  assert.deepEqual(v.errors, []);
  assert.match(v.warnings[0], /"tempo" não é parâmetro/);
});

// Os buracos fechados em 10/11/2026 (escritos sem Node na máquina: nunca rodaram; a intenção está no nome de cada teste)

test('número que não é finito é erro: o JSON "1e999" vira Infinity e chegaria ao app como null', () => {
  const huge = JSON.parse('{ "bpm": 1e999 }');
  assert.equal(huge.bpm, Infinity, 'é assim que o Node lê o número grande demais');
  assert.match(validateParams(huge, setBpm).errors[0], /"bpm" não é um número finito/);
  assert.match(validateParams({ bpm: NaN }, setBpm).errors[0], /"bpm" não é um número finito/);

  // dentro de lista e de objeto também, com o lugar exato
  const nested = validateParams({ channel: 'ch_1', notes: [{ key: 60 }, { key: 60, velocity: -Infinity }] }, addNotes);
  assert.deepEqual(nested.errors, ['"notes[1].velocity" não é um número finito (grande demais, ou NaN)']);
  assert.match(validateParams({ channel: 'ch_1', notes: [], target: { a: { b: Infinity } } }, addNotes).errors[0], /"target\.a\.b"/);
});

test('inteiro grande demais é erro: 1e300 passa no Number.isInteger, mas não é um inteiro que o app consiga ler', () => {
  assert.match(validateParams({ channel: 'ch_1', notes: [], steps: 1e300 }, addNotes).errors[0], /"steps" é grande demais para um inteiro/);
  assert.deepEqual(validateParams({ channel: 'ch_1', notes: [], steps: 2147483647 }, addNotes).errors, []);
  assert.deepEqual(validateParams({ channel: 'ch_1', notes: [], steps: -64 }, addNotes).errors, []);
});

test('nome de parâmetro igual a algo do Object.prototype ("constructor", "toString") é só um parâmetro desconhecido', () => {
  const v = validateParams({ bpm: 150, constructor: 'x', toString: 1 }, setBpm);
  assert.deepEqual(v.errors, []);
  assert.equal(v.warnings.length, 2, 'antes, passavam calados: achavam a função herdada no lugar do schema');
  assert.match(v.warnings[0], /"constructor" não é parâmetro/);

  // e um obrigatório com nome herdado não conta como dado só porque todo objeto tem toString
  const needs = { type: 'object', properties: { toString: { type: 'string', description: 'Texto' } }, required: ['toString'] };
  assert.match(validateParams({}, needs).errors[0], /falta "toString" \(string\): Texto/);
});

test('comando sem schema: nada é obrigatório, e tudo o que vier é aviso', () => {
  assert.deepEqual(validateParams(undefined, undefined), { errors: [], warnings: [] });
  assert.equal(validateParams({ a: 1 }, {}).warnings.length, 1);
});

test('params que não é objeto é recusado', () => {
  assert.match(validateParams([1, 2], setBpm).errors[0], /precisa ser um objeto/);
  assert.match(validateParams('bpm=150', setBpm).errors[0], /precisa ser um objeto/);
});
