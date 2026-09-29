/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { Worker } from 'node:worker_threads';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
const url = pathToFileURL(resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package', 'src/node.mjs')).href;
const source = `const {parentPort,workerData}=require('node:worker_threads');
(async()=>{
 const {Engine,Predicate}=await import(workerData.url);
 for(let i=0;i<30;++i){
  const engine=await Engine.create({profile:workerData.profile});
  try {
   engine.registerDomain('workers',[Predicate.edb('seed',1),Predicate.idbQuery('answer',1)]);
   const rules=engine.loadInlineRuleset('workers','main','answer(X) :- seed(X).');
   const edb=rules.edb(); edb.addFact('seed',[workerData.id]);
   const result=rules.solve(edb); if(!result.containsFact('answer',[workerData.id])) throw new Error('cross-worker data');
  } finally {engine.close();}
 }
 parentPort.postMessage('ok');
})().catch(error=>{throw error;});`;
for (const profile of ['small', 'large']) {
  const result = await Promise.all(Array.from({ length: 4 }, (_, id) => new Promise((resolve, reject) => {
    const w = new Worker(source, { eval: true, workerData: { url, profile, id } });
    let message;
    w.on('message', value => { message = value; }); w.on('error', reject);
    w.on('exit', code => code ? reject(new Error(`worker exit ${code}`)) : resolve(message));
  })));
  assert.deepEqual(result, ['ok', 'ok', 'ok', 'ok']);
  console.log(`Node workers ${profile}: 120 independent lifetimes PASS`);
}
