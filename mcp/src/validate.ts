// Confere os parâmetros de um comando contra o schema que o registro do app publica (type, required, properties).
// É para a IA acertar e corrigir sozinha: a segurança (caminhos, risco, limites) fica no app, que manda.

import type { JsonSchemaObject } from './bridge.js';

export interface Validation {
  errors: string[];
  warnings: string[];
}

const typeOf = (v: unknown): string => {
  if (v === null) return 'null';
  if (Array.isArray(v)) return 'array';
  if (typeof v === 'number') return Number.isInteger(v) ? 'integer' : 'number';
  return typeof v;
};

const matches = (expected: string, v: unknown): boolean => {
  const actual = typeOf(v);

  switch (expected) {
    case 'number': return actual === 'number' || actual === 'integer';
    case 'integer': return actual === 'integer';
    case 'object': return actual === 'object';
    default: return actual === expected;
  }
};

export function validateParams(params: unknown, schema: JsonSchemaObject | undefined): Validation {
  const errors: string[] = [];
  const warnings: string[] = [];

  if (params !== undefined && typeOf(params) !== 'object')
    return { errors: ['params precisa ser um objeto { nome: valor }.'], warnings };

  const given = (params ?? {}) as Record<string, unknown>;
  const properties = schema?.properties ?? {};

  for (const name of schema?.required ?? [])
    if (!(name in given) || given[name] === undefined)
      errors.push(`falta "${name}" (${properties[name]?.type ?? '?'}): ${properties[name]?.description ?? ''}`.trim());

  for (const [name, value] of Object.entries(given)) {
    const spec = properties[name];

    if (!spec) {
      warnings.push(`"${name}" não é parâmetro deste comando e foi ignorado pelo app`);
      continue;
    }

    if (spec.type && !matches(spec.type, value))
      errors.push(`"${name}" precisa ser ${spec.type}, veio ${typeOf(value)}`);
  }

  return { errors, warnings };
}
