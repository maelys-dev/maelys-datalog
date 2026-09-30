/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { resolve, join } from 'node:path';
import { tmpdir } from 'node:os';
const root = resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package');
const temporary = mkdtempSync(join(tmpdir(), 'maelys-npm-consumer-'));
const run = (cmd, args, cwd = temporary) => execFileSync(cmd,args,{cwd,encoding:'utf8',stdio:['ignore','pipe','pipe'],env:{...process.env,NPM_CONFIG_CACHE:join(temporary,'npm-cache')}});
try {
  const packed = JSON.parse(run('npm',['pack','--json','--ignore-scripts','--pack-destination',temporary],root));
  const tar = join(temporary,packed[0].filename);
  writeFileSync(join(temporary,'package.json'), JSON.stringify({private:true}));
  run('npm',['install','--ignore-scripts','--no-audit','--no-fund','--package-lock=false',tar]);
  const fixture = `import {Engine,Predicate,ExplanationKind,Capability,SessionCapacities,ResultTerm, type Value} from '@maelys-dev/datalog';
async function main() {
 const engine=await Engine.create({profile:'small'});
 engine.registerDomain('types',[Predicate.edb('e',1),Predicate.idbQuery('q',1)]);
 const rules=engine.loadInlineRuleset('types','p','q(X) :- e(X).');
 const edb=rules.edb({factCapacity:2,textCapacity:64}); edb.addFact('e',[1n]);
 const session=rules.prepare({capacities:new SessionCapacities({inputFacts:2}),requiredCapabilities:Capability.POSITIVE,explanations:ExplanationKind.TRUE});
 const result=session.solve(edb); const rows:Value[][]=result.enumeratePredicateFacts('q',1);
 const terms:ResultTerm[][]=result.enumerateRaw('q',1); result.resolveTerm(terms[0][0]);
 result.close(); engine[Symbol.dispose](); return rows;
}
// @ts-expect-error input terms cannot contain objects
const bad:import('@maelys-dev/datalog').InputValue = {};
// @ts-expect-error resource constructors are private
new Engine();
// @ts-expect-error result properties are read-only
new SessionCapacities().inputFacts=4;
export {main};
`;
  writeFileSync(join(temporary,'consumer.mts'),fixture);
  writeFileSync(join(temporary,'consumer.cts'),fixture);
  writeFileSync(join(temporary,'browser.ts'),fixture.replaceAll("'@maelys-dev/datalog'","'@maelys-dev/datalog/wasm'"));
  const compiler=process.env.TSC || 'tsc';
  run(compiler,['--strict','--noEmit','--target','ES2022','--module','NodeNext','--moduleResolution','NodeNext','consumer.mts','consumer.cts']);
  run(compiler,['--strict','--noEmit','--target','ES2022','--module','ESNext','--moduleResolution','bundler','browser.ts']);
  for(const runtime of (process.env.MAELYS_JS_RUNTIMES||'node,wasm').split(',')) {
    for(const esm of [false,true]) {
      const imported=esm ? `const {Engine,Predicate}=await import('@maelys-dev/datalog/${runtime}');` : `const {Engine,Predicate}=require('@maelys-dev/datalog/${runtime}');`;
      const code=`${imported} (async()=>{const engine=await Engine.create();try{engine.registerDomain('package',[Predicate.edb('e',1),Predicate.idbQuery('q',1)]);const r=engine.loadInlineRuleset('package','p','q(X) :- e(X).');const input=r.edb();input.addFact('e',[42n]);const result=r.solve(input);if(!result.containsFact('q',[42n]))throw new Error('bad packaged result');}finally{engine.close();}})().catch(e=>{console.error(e);process.exitCode=1;});`;
      run(process.execPath,[...(esm?['--input-type=module']:[]),'-e',code]);
    }
  }
  const installed=JSON.parse(readFileSync(join(temporary,'node_modules/@maelys-dev/datalog/package.json')));
  assert.equal(installed.name,'@maelys-dev/datalog'); assert.equal(installed.scripts,undefined);
  console.log('Packed npm consumer: CJS + ESM, NodeNext + bundler types, typed misuse controls PASS');
} finally { rmSync(temporary,{recursive:true,force:true}); }
