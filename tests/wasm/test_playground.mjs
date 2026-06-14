import MaelysDatalogDynamic from '../../build/wasm/maelys_datalog_dynamic.js';
import playgroundPkg from '../../js/maelys_playground.js';

const { MaelysPlayground, PredKind } = playgroundPkg;

let passed = 0;
let failed = 0;

async function test(name, fn) {
  try {
    await fn();
    console.log(`PASS: ${name}`);
    passed++;
  } catch (err) {
    console.error(`FAIL: ${name}: ${err.message}`);
    failed++;
  }
}

await test('playground_basic', async () => {
  const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
  const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
  pg.domainBegin('access');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('access', 'access.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected alice=true');
  if (pg.querySymbol('allow', 'bob')) throw new Error('expected bob=false');
  pg.freeResult();
});

await test('playground_two_evaluations', async () => {
  const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
  const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
  pg.domainBegin('access2');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('access2', 'access2.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('eval1: alice');
  pg.edbBegin();
  pg.addFact('safe', 'bob');
  pg.solve();
  if (!pg.querySymbol('allow', 'bob')) throw new Error('eval2: bob');
  if (pg.querySymbol('allow', 'alice')) throw new Error('eval2: alice should be absent');
  pg.freeResult();
});

await test('playground_error_throw', async () => {
  const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
  const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
  pg.domainBegin('err_domain');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainCommit();
  let threw = false;
  try {
    pg.loadPolicy('err_domain', 'err.main', 'invalid !!! datalog\n');
  } catch (err) {
    threw = true;
    if (!err.message.includes('loadPolicy')) {
      throw new Error(`expected "loadPolicy" in: ${err.message}`);
    }
  }
  if (!threw) throw new Error('must throw on invalid source');
});

await test('playground_query_unknown_readonly', async () => {
  const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
  const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
  pg.domainBegin('ro_test');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('ro_test', 'ro.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.solve();
  const unknowns = Array.from(
    { length: 512 + 8 },
    (_, i) => `ghost_${String(i).padStart(4, '0')}`,
  );
  for (const u of unknowns) {
    const rc = pg.querySymbol('allow', u);
    if (rc !== false) throw new Error(`expected false for unknown "${u}", got ${rc}`);
  }
  pg.freeResult();
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
