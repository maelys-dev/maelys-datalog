/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { constants } from 'node:os';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { runInNewContext } from 'node:vm';
import { test } from 'node:test';

const root = resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package');
const source = readFileSync(resolve(root, 'src/node.cjs'), 'utf8');
test('unsupported hosts and missing prebuilds explain supported platforms and WASM', async () => {
  for (const [platform, arch, glibc, expected] of [
    ['win32', 'x64', undefined, 'MAELYS_UNSUPPORTED_PLATFORM'],
    ['linux', 'x64', undefined, 'MAELYS_UNSUPPORTED_PLATFORM'], // musl
    ['linux', 'arm64', '2.28', 'MAELYS_UNSUPPORTED_PLATFORM'],
    ['darwin', 'x64', undefined, 'MAELYS_UNSUPPORTED_PLATFORM'],
    ['linux', 'x64', '2.34', 'MAELYS_NATIVE_LOAD_FAILED'],
    ['darwin', 'arm64', undefined, 'MAELYS_NATIVE_LOAD_FAILED'],
  ]) {
    const cause = new Error('missing test prebuild');
    const module = { exports: {} };
    runInNewContext(source, { module, process: { platform, arch, report: { getReport: () => ({ header: { glibcVersionRuntime: glibc } }) } },
      require: name => { if (name === './core.cjs') return { binding: factory => factory }; throw cause; } });
    await assert.rejects(module.exports(), error => {
      assert.equal(error.code, expected);
      assert.match(error.message, /glibc >= 2.34/);
      assert.match(error.message, /macOS arm64 >= 13.5/);
      assert.match(error.message, /@maelys-dev\/datalog\/wasm/);
      if (expected === 'MAELYS_NATIVE_LOAD_FAILED') assert.equal(error.cause, cause);
      return true;
    });
  }
});

test('both profiles coexist even when explicitly loaded with RTLD_GLOBAL', async () => {
  // Registration symbols are the only exported definitions. The engines and
  // their registries must remain private even under global loader visibility.
  for (const profile of ['small', 'large']) {
    const addon = { exports: {} };
    process.dlopen(addon, resolve(root, `prebuilds/${process.platform}-${process.arch}/${profile}.node`),
      constants.dlopen.RTLD_NOW | constants.dlopen.RTLD_GLOBAL);
  }
  const { Engine, Predicate } = await import(pathToFileURL(resolve(root, 'src/node.mjs')));
  const small = await Engine.create({ profile: 'small' });
  const large = await Engine.create({ profile: 'large' });
  try {
    for (const [engine, arity] of [[small, 1], [large, 2]]) {
      engine.registerDomain('profile_isolation', [Predicate.edb('seed', arity), Predicate.idbQuery('answer', arity)]);
      const terms = arity === 1 ? ['X'] : ['X', 'Y'];
      const rules = engine.loadInlineRuleset('profile_isolation', 'main', `answer(${terms}) :- seed(${terms}).`);
      const input = rules.edb(); input.addFact('seed', terms);
      const result = rules.solve(input);
      assert.equal(result.containsFact('answer', terms), true);
    }
  } finally { small.close(); large.close(); }
});
