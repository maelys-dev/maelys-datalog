import MaelysDatalogDynamic from '../../build/wasm/maelys_datalog_dynamic.js';
import playgroundPkg from '../../js/maelys_playground.js';

const {
  MaelysPlayground,
  PredKind,
  MAELYS_ERR_INVALID_ARGUMENT,
  MAELYS_ERR_INVALID_FIELD,
  MAELYS_ERR_INVALID_STATE,
} = playgroundPkg;

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

async function createPlayground() {
  const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
  return MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);
}

function expectThrowRc(fn, expectedRc, label) {
  try {
    fn();
  } catch (err) {
    if (!err.message.includes(`rc=${expectedRc}`)) {
      throw new Error(`${label}: expected rc=${expectedRc}, got ${err.message}`);
    }
    return;
  }
  throw new Error(`${label}: expected throw rc=${expectedRc}`);
}

function expectRc(actual, expected, label) {
  if (actual !== expected) {
    throw new Error(`${label}: expected rc=${expected}, got ${actual}`);
  }
}

async function setupUnaryAccess(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy(domainName, `${domainName}.main`, 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  return pg;
}

async function setupBinaryAccess(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('target', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainAddPredicate('allowed_pair', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy(
    domainName,
    `${domainName}.main`,
    'allow(U) :- owns(U, D), target(D).\nallowed_pair(U, D) :- owns(U, D).\n',
  );
  pg.edbBegin();
  return pg;
}

await test('playground_basic', async () => {
  const pg = await createPlayground();
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
  const pg = await createPlayground();
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
  const pg = await createPlayground();
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
  const pg = await createPlayground();
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

await test('playground_id_unary_round_trip', async () => {
  const pg = await setupUnaryAccess('id_unary');
  const id = pg.internRuntimeSymbol('alice');
  if (!(id > 0)) throw new Error(`expected positive id, got ${id}`);
  const id2 = pg.internRuntimeSymbol('alice');
  if (id2 !== id) throw new Error(`expected idempotent intern ${id}, got ${id2}`);
  pg.addSymbolIdFact('safe', id);
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected alice=true');
  if (pg.querySymbol('allow', 'bob')) throw new Error('expected bob=false');
  pg.freeResult();
});

await test('playground_id_binary_round_trip', async () => {
  const pg = await createPlayground();
  pg.domainBegin('id_binary');
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allowed_pair', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy(
    'id_binary',
    'id_binary.main',
    'allow(U) :- owns(U, D).\nallowed_pair(U, D) :- owns(U, D).\n',
  );
  pg.edbBegin();
  const alice = pg.internRuntimeSymbol('alice');
  const doc = pg.internRuntimeSymbol('doc.pdf');
  pg.addSymbolIdsFact('owns', alice, doc);
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected allow(alice)=true');
  if (!pg.querySymbol2('allowed_pair', 'alice', 'doc.pdf')) {
    throw new Error('expected allowed_pair(alice, doc.pdf)=true');
  }
  pg.freeResult();
});

await test('playground_string_path_equals_symbol_id_sugar', async () => {
  const pg = await setupUnaryAccess('string_sugar_id');
  pg.addFact('safe', 'alice');
  pg.solve();
  const stringPath = pg.querySymbol('allow', 'alice');
  pg.edbBegin();
  const id = pg.internRuntimeSymbol('alice');
  pg.addSymbolIdFact('safe', id);
  pg.solve();
  const idPath = pg.querySymbol('allow', 'alice');
  if (stringPath !== true || idPath !== true || stringPath !== idPath) {
    throw new Error(`expected equivalent true results, string=${stringPath}, id=${idPath}`);
  }
  pg.freeResult();
});

await test('playground_symbol_id_boundary_rejects_invalid_ids', async () => {
  const pg = await setupUnaryAccess('id_boundary');
  const id = pg.internRuntimeSymbol('alice');
  expectThrowRc(() => pg.addSymbolIdFact('safe', 0), MAELYS_ERR_INVALID_ARGUMENT, 'id 0');
  expectThrowRc(() => pg.addSymbolIdFact('safe', -1), MAELYS_ERR_INVALID_ARGUMENT, 'id -1');
  expectThrowRc(
    () => pg.addSymbolIdFact('safe', -2147483648),
    MAELYS_ERR_INVALID_ARGUMENT,
    'id INT32_MIN',
  );
  expectThrowRc(() => pg.addSymbolIdFact('safe', id + 1), MAELYS_ERR_INVALID_ARGUMENT, 'id oob');
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('invalid IDs inserted a fact');
  pg.freeResult();
});

await test('playground_symbol_id_boundary_rejects_null_intern_args', async () => {
  const pg = await setupUnaryAccess('id_nulls');
  const ptr = pg._mod._malloc(4);
  if (!ptr) throw new Error('malloc returned 0');
  try {
    expectRc(
      pg._call('maelys_datalog_wasm_edb_intern_runtime_symbol',
               'number',
               ['number', 'number'],
               [0, ptr]),
      MAELYS_ERR_INVALID_ARGUMENT,
      'intern null text',
    );
    expectRc(
      pg._call('maelys_datalog_wasm_edb_intern_runtime_symbol',
               'number',
               ['string', 'number'],
               ['alice', 0]),
      MAELYS_ERR_INVALID_ARGUMENT,
      'intern null out',
    );
  } finally {
    pg._mod._free(ptr);
  }
});

await test('playground_symbol_id_state_guards', async () => {
  const pg = await createPlayground();
  expectThrowRc(() => pg.internRuntimeSymbol('alice'), MAELYS_ERR_INVALID_STATE, 'empty intern');
  expectThrowRc(() => pg.addSymbolIdFact('safe', 1), MAELYS_ERR_INVALID_STATE, 'empty unary id');
  expectThrowRc(() => pg.addSymbolIdsFact('owns', 1, 1), MAELYS_ERR_INVALID_STATE, 'empty binary ids');
  expectThrowRc(() => pg.addFact('safe', 'alice'), MAELYS_ERR_INVALID_STATE, 'empty string unary');
  expectThrowRc(() => pg.addFact2('owns', 'alice', 'doc.pdf'), MAELYS_ERR_INVALID_STATE, 'empty string binary');

  pg.domainBegin('id_state');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('id_state', 'id_state.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.solve();

  expectThrowRc(() => pg.internRuntimeSymbol('alice'), MAELYS_ERR_INVALID_STATE, 'solved intern');
  expectThrowRc(() => pg.addSymbolIdFact('safe', 1), MAELYS_ERR_INVALID_STATE, 'solved unary id');
  expectThrowRc(() => pg.addSymbolIdsFact('owns', 1, 1), MAELYS_ERR_INVALID_STATE, 'solved binary ids');
  expectThrowRc(() => pg.addFact('safe', 'alice'), MAELYS_ERR_INVALID_STATE, 'solved string unary');
  expectThrowRc(() => pg.addFact2('owns', 'alice', 'doc.pdf'), MAELYS_ERR_INVALID_STATE, 'solved string binary');
  pg.freeResult();
});

await test('playground_symbol_id_propagates_predicate_errors', async () => {
  const pg = await createPlayground();
  pg.domainBegin('id_predicate_errors');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('id_predicate_errors', 'id_predicate_errors.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  const alice = pg.internRuntimeSymbol('alice');
  const doc = pg.internRuntimeSymbol('doc.pdf');
  expectThrowRc(
    () => pg.addSymbolIdFact('unknown', alice),
    MAELYS_ERR_INVALID_FIELD,
    'unknown predicate',
  );
  expectThrowRc(
    () => pg.addSymbolIdFact('owns', alice),
    MAELYS_ERR_INVALID_FIELD,
    'unary into binary predicate',
  );
  expectThrowRc(
    () => pg.addSymbolIdsFact('safe', alice, doc),
    MAELYS_ERR_INVALID_FIELD,
    'binary into unary predicate',
  );
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('failed predicate calls inserted a fact');
  pg.freeResult();
});

await test('playground_symbol_id_session_reset_first_id_stable', async () => {
  const pg = await setupUnaryAccess('id_reset');
  const first = pg.internRuntimeSymbol('alice');
  if (first !== 1) throw new Error(`expected first intern id 1, got ${first}`);
  pg.edbBegin();
  const second = pg.internRuntimeSymbol('alice');
  if (second !== 1) throw new Error(`expected reset intern id 1, got ${second}`);
  pg.freeResult();
});

await test('playground_batch_id_unary_round_trip', async () => {
  const pg = await setupUnaryAccess('batch_unary');
  const alice = pg.internRuntimeSymbol('alice');
  const bob = pg.internRuntimeSymbol('bob');
  pg.addSymbolIdFacts('safe', [alice, bob]);
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected alice=true');
  if (!pg.querySymbol('allow', 'bob')) throw new Error('expected bob=true');
  if (pg.querySymbol('allow', 'mallory')) throw new Error('expected mallory=false');
  pg.freeResult();
});

await test('playground_batch_id_binary_round_trip', async () => {
  const pg = await setupBinaryAccess('batch_binary');
  const alice = pg.internRuntimeSymbol('alice');
  const doc = pg.internRuntimeSymbol('doc.pdf');
  const bob = pg.internRuntimeSymbol('bob');
  const other = pg.internRuntimeSymbol('other.pdf');
  pg.addSymbolIdFact('target', doc);
  pg.addSymbolIdsFacts('owns', [alice, doc, bob, other]);
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected allow(alice)=true');
  if (pg.querySymbol('allow', 'bob')) throw new Error('expected allow(bob)=false');
  if (!pg.querySymbol2('allowed_pair', 'alice', 'doc.pdf')) {
    throw new Error('expected allowed_pair(alice, doc.pdf)=true');
  }
  if (!pg.querySymbol2('allowed_pair', 'bob', 'other.pdf')) {
    throw new Error('expected allowed_pair(bob, other.pdf)=true');
  }
  pg.freeResult();
});

await test('playground_batch_id_empty_batch_ok', async () => {
  const pg = await setupUnaryAccess('batch_empty_unary');
  pg.addSymbolIdFacts('safe', []);
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('empty unary batch inserted a fact');
  pg.freeResult();

  const pg2 = await setupBinaryAccess('batch_empty_binary');
  pg2.addSymbolIdsFacts('owns', []);
  pg2.solve();
  if (pg2.querySymbol('allow', 'alice')) throw new Error('empty binary batch inserted a fact');
  pg2.freeResult();
});

await test('playground_batch_id_boundary_rejects_invalid_ids', async () => {
  const pg = await setupUnaryAccess('batch_invalid_ids');
  const alice = pg.internRuntimeSymbol('alice');
  const bob = pg.internRuntimeSymbol('bob');
  expectThrowRc(() => pg.addSymbolIdFacts('safe', [alice, 0, bob]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch unary id 0');
  expectThrowRc(() => pg.addSymbolIdFacts('safe', [-1]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch unary id -1');
  expectThrowRc(() => pg.addSymbolIdFacts('safe', [-2147483648]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch unary INT32_MIN');
  expectThrowRc(() => pg.addSymbolIdFacts('safe', [bob + 1]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch unary out of range');
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('invalid unary batch inserted alice');
  if (pg.querySymbol('allow', 'bob')) throw new Error('invalid unary batch inserted bob');
  pg.freeResult();

  const pg2 = await setupBinaryAccess('batch_invalid_pair_ids');
  const alice2 = pg2.internRuntimeSymbol('alice');
  const doc = pg2.internRuntimeSymbol('doc.pdf');
  expectThrowRc(() => pg2.addSymbolIdsFacts('owns', [alice2, doc, 0, doc]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch binary id 0');
  expectThrowRc(() => pg2.addSymbolIdsFacts('owns', [alice2, -1]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch binary id -1');
  expectThrowRc(() => pg2.addSymbolIdsFacts('owns', [alice2, -2147483648]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch binary INT32_MIN');
  expectThrowRc(() => pg2.addSymbolIdsFacts('owns', [alice2, doc + 1]),
                MAELYS_ERR_INVALID_ARGUMENT,
                'batch binary out of range');
  pg2.solve();
  if (pg2.querySymbol('allow', 'alice')) throw new Error('invalid binary batch inserted alice');
  pg2.freeResult();
});

await test('playground_batch_id_negative_count_rejected', async () => {
  const pg = await setupUnaryAccess('batch_negative_count');
  expectRc(
    pg._call('maelys_datalog_wasm_edb_add_symbol_id_facts',
             'number',
             ['string', 'number', 'number'],
             ['safe', 0, -1]),
    MAELYS_ERR_INVALID_ARGUMENT,
    'unary negative count',
  );
  expectRc(
    pg._call('maelys_datalog_wasm_edb_add_symbol_ids_facts',
             'number',
             ['string', 'number', 'number'],
             ['owns', 0, -1]),
    MAELYS_ERR_INVALID_ARGUMENT,
    'binary negative count',
  );
  pg.freeResult();
});

await test('playground_batch_id_odd_flat_length_rejected_in_js', async () => {
  const pg = await setupBinaryAccess('batch_odd_flat');
  const alice = pg.internRuntimeSymbol('alice');
  try {
    pg.addSymbolIdsFacts('owns', [alice]);
  } catch (err) {
    if (!(err instanceof TypeError)) {
      throw new Error(`expected TypeError for odd flat pairs, got ${err.message}`);
    }
    pg.freeResult();
    return;
  }
  throw new Error('expected TypeError for odd flat pairs');
});

await test('playground_batch_id_predicate_errors_propagated', async () => {
  const pg = await createPlayground();
  pg.domainBegin('batch_predicate_errors');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('batch_predicate_errors', 'batch_predicate_errors.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  const alice = pg.internRuntimeSymbol('alice');
  const bob = pg.internRuntimeSymbol('bob');
  expectThrowRc(() => pg.addSymbolIdFacts('missing', [alice]),
                MAELYS_ERR_INVALID_FIELD,
                'batch unknown predicate');
  expectThrowRc(() => pg.addSymbolIdFacts('owns', [alice]),
                MAELYS_ERR_INVALID_FIELD,
                'batch unary into binary predicate');
  expectThrowRc(() => pg.addSymbolIdsFacts('safe', [alice, bob]),
                MAELYS_ERR_INVALID_FIELD,
                'batch binary into unary predicate');
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('failed batch predicate calls inserted a fact');
  pg.freeResult();
});

await test('playground_batch_id_state_guards', async () => {
  const pg = await createPlayground();
  expectThrowRc(() => pg.addSymbolIdFacts('safe', []),
                MAELYS_ERR_INVALID_STATE,
                'empty-state unary batch');
  expectThrowRc(() => pg.addSymbolIdsFacts('owns', []),
                MAELYS_ERR_INVALID_STATE,
                'empty-state binary batch');

  pg.domainBegin('batch_state');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy('batch_state', 'batch_state.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.solve();
  expectThrowRc(() => pg.addSymbolIdFacts('safe', []),
                MAELYS_ERR_INVALID_STATE,
                'solved-state unary batch');
  expectThrowRc(() => pg.addSymbolIdsFacts('owns', []),
                MAELYS_ERR_INVALID_STATE,
                'solved-state binary batch');
  pg.freeResult();
});

await test('playground_batch_id_string_path_still_works', async () => {
  const pg = await createPlayground();
  pg.domainBegin('batch_string_paths');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainAddPredicate('allowed_pair', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadPolicy(
    'batch_string_paths',
    'batch_string_paths.main',
    'allow(X) :- safe(X).\nallowed_pair(U, D) :- owns(U, D).\n',
  );
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.addFact2('owns', 'alice', 'doc.pdf');
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('string addFact regressed');
  if (!pg.querySymbol2('allowed_pair', 'alice', 'doc.pdf')) {
    throw new Error('string addFact2 regressed');
  }
  pg.freeResult();
});

await test('playground_batch_id_equals_repeated_single_id_calls', async () => {
  const pgA = await setupUnaryAccess('batch_equals_unit_a');
  const aliceA = pgA.internRuntimeSymbol('alice');
  const bobA = pgA.internRuntimeSymbol('bob');
  pgA.addSymbolIdFacts('safe', [aliceA, bobA]);
  pgA.solve();
  const batchAlice = pgA.querySymbol('allow', 'alice');
  const batchBob = pgA.querySymbol('allow', 'bob');
  pgA.freeResult();

  const pgB = await setupUnaryAccess('batch_equals_unit_b');
  const aliceB = pgB.internRuntimeSymbol('alice');
  const bobB = pgB.internRuntimeSymbol('bob');
  pgB.addSymbolIdFact('safe', aliceB);
  pgB.addSymbolIdFact('safe', bobB);
  pgB.solve();
  if (batchAlice !== pgB.querySymbol('allow', 'alice')) throw new Error('alice mismatch');
  if (batchBob !== pgB.querySymbol('allow', 'bob')) throw new Error('bob mismatch');
  pgB.freeResult();

  const pgC = await setupBinaryAccess('batch_equals_unit_c');
  const aliceC = pgC.internRuntimeSymbol('alice');
  const docC = pgC.internRuntimeSymbol('doc.pdf');
  const bobC = pgC.internRuntimeSymbol('bob');
  const otherC = pgC.internRuntimeSymbol('other.pdf');
  pgC.addSymbolIdFact('target', docC);
  pgC.addSymbolIdsFacts('owns', [aliceC, docC, bobC, otherC]);
  pgC.solve();
  const batchAllowAlice = pgC.querySymbol('allow', 'alice');
  const batchAllowBob = pgC.querySymbol('allow', 'bob');
  pgC.freeResult();

  const pgD = await setupBinaryAccess('batch_equals_unit_d');
  const aliceD = pgD.internRuntimeSymbol('alice');
  const docD = pgD.internRuntimeSymbol('doc.pdf');
  const bobD = pgD.internRuntimeSymbol('bob');
  const otherD = pgD.internRuntimeSymbol('other.pdf');
  pgD.addSymbolIdFact('target', docD);
  pgD.addSymbolIdsFact('owns', aliceD, docD);
  pgD.addSymbolIdsFact('owns', bobD, otherD);
  pgD.solve();
  if (batchAllowAlice !== pgD.querySymbol('allow', 'alice')) throw new Error('binary alice mismatch');
  if (batchAllowBob !== pgD.querySymbol('allow', 'bob')) throw new Error('binary bob mismatch');
  pgD.freeResult();
});

await test('playground_batch_id_binary_not_half_capped_by_flat_scratch', async () => {
  const pg = await setupBinaryAccess('batch_scratch_full');
  const alice = pg.internRuntimeSymbol('alice');
  const doc = pg.internRuntimeSymbol('doc.pdf');
  const pairs = [];
  for (let i = 0; i < 513; i++) {
    pairs.push(alice, doc);
  }
  expectThrowRc(() => pg.addSymbolIdsFacts('missing', pairs),
                MAELYS_ERR_INVALID_FIELD,
                'batch pair_count above half MAX should reach core predicate lookup');
  pg.freeResult();
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
