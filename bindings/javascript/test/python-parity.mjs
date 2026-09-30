/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
const root = resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package');
const f = (predicate, ...terms) => ({ predicate, terms });
const p = (name, arity, flags) => ({ name, arity, flags });
const cases = [{
  domain: 'js_document_access', predicates: [p('member',2,1),p('document',2,1),p('blocked',1,1),p('can_read',2,6)],
  source:'can_read(U,D) :- member(U,G), document(D,G), not(blocked(U)).',
  facts:[f('member','alice','engineering'),f('member','bob','engineering'),f('member','carol','finance'),f('document','design','engineering'),f('document','roadmap','engineering'),f('document','budget','finance'),f('blocked','bob')],
  queries:[f('can_read','alice','design'),f('can_read','alice','roadmap'),f('can_read','bob','design'),f('can_read','carol','budget'),f('can_read','carol','design')],
  documents:[{...f('can_read','alice','design'),kind:'true'},{...f('can_read','bob','design'),kind:'false'}],
}, {
  domain:'js_depth', predicates:[p('edge',2,1),p('path',2,6),p('reach',2,6)],
  source:'path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y),edge(Y,Z). reach(X,Y) :- path(X,Y).',
  facts:Array.from({length:8},(_,i)=>f('edge',`n${i}`,`n${i+1}`)), queries:[f('path','n0','n8')],
  documents:[{...f('path','n0','n8'),kind:'true'}],
}, {
  domain:'js_false_limit', predicates:[p('edge',2,1),p('never',1,1),p('q',1,6)], source:'q(X) :- edge(X,Y),never(Y).',
  facts:Array.from({length:40},(_,i)=>f('edge','x',i)), queries:[f('q','x')],documents:[{...f('q','x'),kind:'false'}],
}];
for (const testcase of cases) for (const explanations of [0,3]) {
  testcase.explanations = explanations;
  const python = spawnSync(process.env.PYTHON || 'python3', ['bindings/javascript/test/python-oracle.py'], { input:JSON.stringify(testcase),encoding:'utf8' });
  assert.equal(python.status,0,python.stderr);
  const expected = JSON.parse(python.stdout);
  for (const runtime of ['node','wasm']) {
    const {Engine} = await import(pathToFileURL(resolve(root,`src/${runtime}.mjs`)));
    const engine = await Engine.create({profile:process.env.MAELYS_PROFILE||'small'});
    try {
      engine.registerDomain(testcase.domain,testcase.predicates);
      const rules = engine.loadInlineRuleset(testcase.domain,'main',testcase.source),edb=rules.edb();edb.addFacts(testcase.facts);
      const result=rules.solve(edb,{explanations});
      const actual={answers:testcase.queries.map(q=>result.containsFact(q.predicate,q.terms)),
        documents:testcase.documents.map(q=>result[q.kind==='true'?'explainTrue':'explainFalse'](q.predicate,q.terms)),
        policy:rules.fingerprint,execution:result.executionFingerprint,
        counts:rules.programCounts(),policyAtomLimits:[engine.limits.maxPolicyAtoms,engine.limits.maxPolicyAtomBytes]};
      assert.deepEqual(actual,expected);
    }finally{engine.close();}
  }
  const state=testcase.domain==='js_document_access'?'complete':'truncated';
  for(const document of expected.documents) assert.match(document,new RegExp(`status=${state}`));
}
console.log('Python/native Node/WASM: decisions, fingerprints, complete and truncated explanations agree byte-for-byte (workspace off/on)');
