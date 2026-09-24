import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const {MaelysPlayground, PredKind, Status} = require('../../bindings/wasm/maelys_playground.js');
const declaration = readFileSync(new URL('../../bindings/wasm/maelys_playground.d.ts', import.meta.url), 'utf8');

test('declarations cover the exact runtime method and constant surface', () => {
  // The separate TypeScript consumer checks signatures, outputs and rejected
  // legacy calls; this comparison also fails if a public method is omitted.
  const body = declaration.split('export class MaelysPlayground {')[1];
  const declared = [...body.matchAll(/^  (\w+)\(/gm)].map(m => m[1]).filter(n => n !== 'constructor').sort();
  const runtime = Object.getOwnPropertyNames(MaelysPlayground.prototype).filter(n => n !== 'constructor' && !n.startsWith('_')).sort();
  assert.deepEqual(declared, runtime);
  assert.match(body, /static create\(/);
  assert.equal(typeof MaelysPlayground.create, 'function');
  for (const [name, object] of Object.entries({PredKind, Status})) {
    const section = declaration.split(`export const ${name}: Readonly<{`)[1].split('}>;')[0];
    const values = Object.fromEntries([...section.matchAll(/(\w+): (-?\d+)/g)].map(m => [m[1], Number(m[2])]));
    assert.deepEqual(values, object);
  }
});
