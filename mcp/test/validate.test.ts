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

test('params que não é objeto é recusado', () => {
  assert.match(validateParams([1, 2], setBpm).errors[0], /precisa ser um objeto/);
  assert.match(validateParams('bpm=150', setBpm).errors[0], /precisa ser um objeto/);
});
