/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { pathToFileURL } from 'node:url';
const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || '@playwright/test');
const root = resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package');
const server = createServer(async (req, res) => {
  try {
    const name = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
    if (name === '/') { res.setHeader('content-type', 'text/html'); res.end('<!doctype html><title>Binding test</title>'); return; }
    const path = resolve(root, `.${name}`);
    if (!path.startsWith(root + '/')) throw new Error('bad path');
    const bytes = await readFile(path);
    res.setHeader('content-type', extname(path) === '.wasm' ? 'application/wasm' : 'text/javascript'); res.end(bytes);
  } catch { res.statusCode = 404; res.end(); }
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const browser = await chromium.launch({ channel: process.env.PLAYWRIGHT_CHANNEL || 'chrome', headless: true });
try {
  const page = await browser.newPage(), errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  for (const profile of ['small', 'large']) {
    const actual = await page.evaluate(async profile => {
      const url = new URL('/src/wasm.mjs', location.href).href;
      const run = async (url, profile) => {
        const { Engine, Predicate } = await import(url);
        const engine = await Engine.create({ profile });
        try {
          engine.registerDomain('browser', [Predicate.edb('seed', 1), Predicate.idbQuery('answer', 1)]);
          const rules = engine.loadInlineRuleset('browser', 'main', 'answer(X) :- seed(X).');
          const edb = rules.edb(); edb.addFact('seed', [9223372036854775807n]);
          const result = rules.solve(edb, { explanations: 1 });
          const tx=rules.prepare().inputs({factCapacity:2});
          tx.replace(tx.base,[{predicate:'seed',terms:[1n]}]).close();
          const changed=tx.apply(tx.base,{added:[{predicate:'seed',terms:[9223372036854775807n]}],removed:[{predicate:'seed',terms:[1n]}]});
          const transaction=changed.enumeratePredicateFacts('answer',1).map(row=>row.map(String));
          return { transaction, values: result.enumeratePredicateFacts('answer', 1).map(row => row.map(String)),
            text: result.explainTrue('answer', [9223372036854775807n]), limit: engine.limits.maxEdbFacts };
        } finally { engine.close(); }
      };
      const direct = await run(url, profile);
      const source = `const run=${run.toString()}; self.onmessage=async({data})=>{try {self.postMessage({value:await run(...data)});}catch(error){self.postMessage({error:String(error)});}};`;
      const blob = URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
      const worker = new Worker(blob, { type: 'module' });
      try {
        const background = await new Promise((resolve, reject) => {
          worker.onmessage = ({ data }) => data.error ? reject(new Error(data.error)) : resolve(data.value);
          worker.onerror = reject; worker.postMessage([url, profile]);
        });
        return { direct, background };
      } finally { worker.terminate(); URL.revokeObjectURL(blob); }
    }, profile);
    assert.deepEqual(actual.direct, actual.background);
    assert.deepEqual(actual.direct.values, [['9223372036854775807']]);
    assert.deepEqual(actual.direct.transaction, actual.direct.values);
    assert.match(actual.direct.text, /status=complete/);
    console.log(`browser + module worker ${profile}: same API, int64 and canonical explanation PASS`);
  }
  assert.deepEqual(errors, []);
} finally { await browser.close(); await new Promise(r => server.close(r)); }
