// Confere os parâmetros de um comando contra o schema que o registro do app publica (type, required, properties).
// É para a IA acertar e corrigir sozinha: a segurança (caminhos, risco, limites) fica no app, que manda.
// O schema do app só traz o tipo, a descrição e os obrigatórios: não há enum, mínimo nem máximo para conferir aqui
// (as faixas e os valores aceitos estão na descrição, e quem recusa é o app). O tamanho do pedido é da ponte (bridge.ts).

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

/** Onde há um número que não é finito (o JSON "1e999" vira Infinity, e voltaria ao app como null), ou null se não há.
    Olha dentro de listas e objetos, até uma profundidade que nenhum comando usa. */
function nonFinitePath(value: unknown, path: string, depth = 0): string | null {
  if (typeof value === 'number')
    return Number.isFinite(value) ? null : path;

  if (value === null || typeof value !== 'object' || depth > 16)
    return null;

  const inside: [string, unknown][] = Array.isArray(value)
    ? value.map((inner: unknown, i: number): [string, unknown] => [`${path}[${i}]`, inner])
    : Object.entries(value as Record<string, unknown>).map(([key, inner]): [string, unknown] => [`${path}.${key}`, inner]);

  for (const [where, inner] of inside) {
    const found = nonFinitePath(inner, where, depth + 1);

    if (found)
      return found;
  }

  return null;
}

export function validateParams(params: unknown, schema: JsonSchemaObject | undefined): Validation {
  const errors: string[] = [];
  const warnings: string[] = [];

  if (params !== undefined && typeOf(params) !== 'object')
    return { errors: ['params precisa ser um objeto { nome: valor }.'], warnings };

  const given = (params ?? {}) as Record<string, unknown>;
  const properties = schema?.properties ?? {};

  // Object.hasOwn: um parâmetro chamado "constructor" ou "toString" não pode achar o que vem do Object.prototype
  const specOf = (name: string) => (Object.hasOwn(properties, name) ? properties[name] : undefined);

  for (const name of schema?.required ?? [])
    if (!Object.hasOwn(given, name) || given[name] === undefined)
      errors.push(`falta "${name}" (${specOf(name)?.type ?? '?'}): ${specOf(name)?.description ?? ''}`.trim());

  for (const [name, value] of Object.entries(given)) {
    const spec = specOf(name);

    if (!spec) {
      warnings.push(`"${name}" não é parâmetro deste comando e foi ignorado pelo app`);
      continue;
    }

    const broken = nonFinitePath(value, name);

    if (broken) {
      errors.push(`"${broken}" não é um número finito (grande demais, ou NaN)`);
      continue;
    }

    if (spec.type && !matches(spec.type, value)) {
      errors.push(`"${name}" precisa ser ${spec.type}, veio ${typeOf(value)}`);
      continue;
    }

    // 1e300 passa no Number.isInteger, mas não é inteiro exato nem cabe no inteiro do app (a faixa de cada um é do app)
    if (spec.type === 'integer' && !Number.isSafeInteger(value))
      errors.push(`"${name}" é grande demais para um inteiro`);
  }

  return { errors, warnings };
}
