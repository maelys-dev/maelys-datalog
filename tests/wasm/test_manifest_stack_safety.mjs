/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { create, pred, P } from './helpers.mjs';
const pg=await create();
pg.registerDomain({name:'stack',predicates:[pred('e',1,P.EDB),pred('q',1,P.IDB|P.QUERY)]});
// Source exceeds Emscripten's default stack. It travels in a heap frame.
const padding='%'+ 'x'.repeat(100000)+'\n';
for(let i=0;i<3;++i) {
  assert.throws(()=>pg.loadPolicy('stack','bad',padding+'q(X) :- e(Y).'),e=>e.diagnostic.message.includes('head variable'));
  pg.loadPolicy('stack','ok',padding+'q(X) :- e(X).');
}
pg.solve(); assert.equal(pg.derivedFactCount(),0); pg.close();
console.log('PASS: heap source transport and repeated rejected-load recovery');
