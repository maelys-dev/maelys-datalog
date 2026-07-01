import MaelysDatalogDynamic from '../../build/wasm/maelys_datalog_dynamic.js';
import playgroundPkg from '../../js/maelys_playground.js';

const {
  MaelysPlayground,
  PredKind,
  MAELYS_ERR_INVALID_ARGUMENT,
  MAELYS_ERR_INVALID_FIELD,
  MAELYS_ERR_PAYLOAD_TOO_LARGE,
  MAELYS_ERR_INVALID_STATE,
  MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX,
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

function expectThrowType(fn, type, label) {
  try {
    fn();
  } catch (err) {
    if (!(err instanceof type)) {
      throw new Error(`${label}: expected ${type.name}, got ${err.constructor.name}: ${err.message}`);
    }
    return;
  }
  throw new Error(`${label}: expected ${type.name}`);
}

function packStrings(pg, values) {
  const byteLen = values.reduce((sum, value) => sum + pg._mod.lengthBytesUTF8(value) + 1, 0);
  const ptr = pg._mod._malloc(byteLen);
  if (!ptr) throw new Error('packStrings malloc returned 0');
  let offset = 0;
  for (const value of values) {
    const segmentBytes = pg._mod.lengthBytesUTF8(value) + 1;
    pg._mod.stringToUTF8(value, ptr + offset, segmentBytes);
    offset += segmentBytes;
  }
  return { ptr, byteLen };
}

async function setupUnaryAccess(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(domainName, `${domainName}.main`, 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset(
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
  pg.loadRuleset('access', 'access.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset('access2', 'access2.main', 'allow(X) :- safe(X).\n');
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
  if (pg.diagHint !== '') throw new Error(`expected empty initial diag hint, got "${pg.diagHint}"`);
  pg.domainBegin('err_domain');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainCommit();
  let threw = false;
  try {
    pg.loadRuleset('err_domain', 'err.main', 'invalid !!! datalog\n');
  } catch (err) {
    threw = true;
    if (!err.message.includes('loadRuleset')) {
      throw new Error(`expected "loadRuleset" in: ${err.message}`);
    }
  }
  if (!threw) throw new Error('must throw on invalid source');
});

await test('playground_diagnostic_hint_round_trip', async () => {
  const pg = await createPlayground();
  pg.domainBegin('hint_domain');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  let threw = false;
  try {
    pg.loadRuleset('hint_domain', 'hint.main', 'allow(X) :- safe(Y).\n');
  } catch {
    threw = true;
  }
  if (!threw) throw new Error('unsafe rule must throw');
  const expectedMessage = 'head variable not bound by positive body atom';
  const expectedHint = 'bind every head variable in a positive body atom';
  if (pg.diagMessage !== expectedMessage) {
    throw new Error(`expected message "${expectedMessage}", got "${pg.diagMessage}"`);
  }
  if (pg.diagHint !== expectedHint) {
    throw new Error(`expected hint "${expectedHint}", got "${pg.diagHint}"`);
  }
});

await test('playground_query_unknown_readonly', async () => {
  const pg = await createPlayground();
  pg.domainBegin('ro_test');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset('ro_test', 'ro.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset(
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
  pg.loadRuleset('id_state', 'id_state.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset('id_predicate_errors', 'id_predicate_errors.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset('batch_predicate_errors', 'batch_predicate_errors.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset('batch_state', 'batch_state.main', 'allow(X) :- safe(X).\n');
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
  pg.loadRuleset(
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

await test('playground_string_batch_unary_round_trip', async () => {
  const pg = await setupUnaryAccess('string_batch_unary');
  pg.addRuntimeSymbolFacts('safe', ['alice', 'bob']);
  pg.solve();
  if (!pg.querySymbol('allow', 'alice')) throw new Error('expected alice=true');
  if (!pg.querySymbol('allow', 'bob')) throw new Error('expected bob=true');
  if (pg.querySymbol('allow', 'mallory')) throw new Error('expected mallory=false');
  pg.freeResult();
});

await test('playground_string_batch_binary_round_trip', async () => {
  const pg = await setupBinaryAccess('string_batch_binary');
  pg.addFact('target', 'doc.pdf');
  pg.addRuntimeSymbolPairFacts('owns', ['alice', 'doc.pdf', 'bob', 'other.pdf']);
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

await test('playground_string_batch_empty_batch_ok', async () => {
  const pg = await setupUnaryAccess('string_batch_empty_unary');
  pg.addRuntimeSymbolFacts('safe', []);
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('empty unary string batch inserted a fact');
  pg.freeResult();

  const pg2 = await setupBinaryAccess('string_batch_empty_binary');
  pg2.addRuntimeSymbolPairFacts('owns', []);
  pg2.solve();
  if (pg2.querySymbol('allow', 'alice')) throw new Error('empty binary string batch inserted a fact');
  pg2.freeResult();
});

await test('playground_string_batch_js_boundary_rejects_bad_arrays', async () => {
  const pg = await setupBinaryAccess('string_batch_js_bad_arrays');
  expectThrowType(() => pg.addRuntimeSymbolFacts('safe', 'alice'), TypeError, 'unary non-array');
  expectThrowType(() => pg.addRuntimeSymbolFacts('safe', ['alice', 42]), TypeError, 'unary non-string');
  expectThrowType(() => pg.addRuntimeSymbolPairFacts('owns', ['alice']), TypeError, 'odd flat pairs');
  expectThrowType(
    () => pg.addRuntimeSymbolPairFacts('owns', ['alice', {}]),
    TypeError,
    'binary non-string',
  );
  pg.freeResult();
});

await test('playground_string_batch_js_rejects_oversize_before_malloc', async () => {
  const pg = await setupUnaryAccess('string_batch_js_range');
  const originalLen = pg._mod.lengthBytesUTF8;
  const originalMalloc = pg._mod._malloc;
  let mallocCalled = false;
  pg._mod._malloc = (bytes) => {
    mallocCalled = true;
    return originalMalloc(bytes);
  };
  try {
    pg._mod.lengthBytesUTF8 = () => 0x7fffffff;
    expectThrowType(() => pg.addRuntimeSymbolFacts('safe', ['x']), RangeError, 'int32 byteLen');
    pg._mod.lengthBytesUTF8 = () => MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX;
    expectThrowType(() => pg.addRuntimeSymbolFacts('safe', ['x']), RangeError, 'wasm byteLen');
  } finally {
    pg._mod.lengthBytesUTF8 = originalLen;
    pg._mod._malloc = originalMalloc;
  }
  if (mallocCalled) throw new Error('oversize JS guard called malloc');
  pg.freeResult();
});

await test('playground_string_batch_raw_boundary_rejects_invalid_packed_args', async () => {
  const pg = await setupUnaryAccess('string_batch_raw_invalid');
  expectRc(
    pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
             'number',
             ['string', 'number', 'number', 'number'],
             ['safe', 0, 1, 1]),
    MAELYS_ERR_INVALID_ARGUMENT,
    'unary null packed',
  );
  expectRc(
    pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
             'number',
             ['string', 'number', 'number', 'number'],
             ['safe', 0, 0, -1]),
    MAELYS_ERR_INVALID_ARGUMENT,
    'unary negative count',
  );
  const tiny = pg._mod._malloc(1);
  if (!tiny) throw new Error('malloc returned 0');
  try {
    expectRc(
      pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
               'number',
               ['string', 'number', 'number', 'number'],
               ['safe', tiny, -1, 1]),
      MAELYS_ERR_INVALID_ARGUMENT,
      'unary negative byte_len',
    );
    expectRc(
      pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
               'number',
               ['string', 'number', 'number', 'number'],
               ['safe', tiny, MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX + 1, 1]),
      MAELYS_ERR_PAYLOAD_TOO_LARGE,
      'unary packed byte_len boundary',
    );
    expectRc(
      pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_pair_facts',
               'number',
               ['string', 'number', 'number', 'number'],
               ['owns', tiny, MAELYS_DATALOG_WASM_PACKED_STRING_BYTES_MAX + 1, 1]),
      MAELYS_ERR_PAYLOAD_TOO_LARGE,
      'binary packed byte_len boundary',
    );
  } finally {
    pg._mod._free(tiny);
  }
  pg.freeResult();
});

await test('playground_string_batch_raw_boundary_rejects_bad_framing', async () => {
  const pg = await setupUnaryAccess('string_batch_raw_framing');
  const packed = packStrings(pg, ['alice', 'bob']);
  try {
    expectRc(
      pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
               'number',
               ['string', 'number', 'number', 'number'],
               ['safe', packed.ptr, packed.byteLen - 1, 2]),
      MAELYS_ERR_INVALID_ARGUMENT,
      'missing final NUL',
    );
  } finally {
    pg._mod._free(packed.ptr);
  }

  const trailing = packStrings(pg, ['alice', 'bob', 'junk']);
  try {
    expectRc(
      pg._call('maelys_datalog_wasm_edb_add_runtime_symbol_facts',
               'number',
               ['string', 'number', 'number', 'number'],
               ['safe', trailing.ptr, trailing.byteLen, 2]),
      MAELYS_ERR_INVALID_ARGUMENT,
      'trailing bytes after expected segments',
    );
  } finally {
    pg._mod._free(trailing.ptr);
  }
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('malformed buffers inserted a fact');
  pg.freeResult();
});

await test('playground_string_batch_empty_string_follows_core_semantics', async () => {
  const pg = await setupUnaryAccess('string_batch_empty_string');
  pg.addRuntimeSymbolFacts('safe', ['']);
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('unexpected alice fact');
  pg.freeResult();
});

await test('playground_string_batch_predicate_errors_propagated', async () => {
  const pg = await createPlayground();
  pg.domainBegin('string_batch_predicate_errors');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset('string_batch_predicate_errors', 'string_batch_predicate_errors.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  expectThrowRc(() => pg.addRuntimeSymbolFacts('missing', ['alice']),
                MAELYS_ERR_INVALID_FIELD,
                'unknown predicate');
  expectThrowRc(() => pg.addRuntimeSymbolFacts('owns', ['alice']),
                MAELYS_ERR_INVALID_FIELD,
                'unary into binary predicate');
  expectThrowRc(() => pg.addRuntimeSymbolPairFacts('safe', ['alice', 'bob']),
                MAELYS_ERR_INVALID_FIELD,
                'binary into unary predicate');
  pg.solve();
  if (pg.querySymbol('allow', 'alice')) throw new Error('predicate errors inserted a fact');
  pg.freeResult();
});

await test('playground_string_batch_capacity_failure_is_atomic', async () => {
  const pg = await setupUnaryAccess('string_batch_capacity');
  const values = Array.from({ length: 65 }, (_, i) => `user_${i}`);
  expectThrowRc(() => pg.addRuntimeSymbolFacts('safe', values),
                MAELYS_ERR_PAYLOAD_TOO_LARGE,
                'per-predicate capacity');
  pg.solve();
  if (pg.querySymbol('allow', 'user_0')) throw new Error('capacity failure inserted user_0');
  if (pg.querySymbol('allow', 'user_64')) throw new Error('capacity failure inserted user_64');
  pg.freeResult();
});

await test('playground_string_batch_overlong_segment_rejected_no_fact', async () => {
  const pg = await setupUnaryAccess('string_batch_overlong');
  const overlong = 'x'.repeat(1025);
  expectThrowRc(() => pg.addRuntimeSymbolFacts('safe', [overlong]),
                MAELYS_ERR_PAYLOAD_TOO_LARGE,
                'overlong segment');
  pg.solve();
  if (pg.querySymbol('allow', 'x')) throw new Error('overlong segment inserted a fact');
  pg.freeResult();
});

await test('playground_string_batch_equals_repeated_string_calls', async () => {
  const pgA = await setupUnaryAccess('string_batch_equals_a');
  pgA.addRuntimeSymbolFacts('safe', ['alice', 'bob']);
  pgA.solve();
  const batchAlice = pgA.querySymbol('allow', 'alice');
  const batchBob = pgA.querySymbol('allow', 'bob');
  pgA.freeResult();

  const pgB = await setupUnaryAccess('string_batch_equals_b');
  pgB.addFact('safe', 'alice');
  pgB.addFact('safe', 'bob');
  pgB.solve();
  if (batchAlice !== pgB.querySymbol('allow', 'alice')) throw new Error('alice mismatch');
  if (batchBob !== pgB.querySymbol('allow', 'bob')) throw new Error('bob mismatch');
  pgB.freeResult();

  const pgC = await setupBinaryAccess('string_batch_equals_c');
  pgC.addFact('target', 'doc.pdf');
  pgC.addRuntimeSymbolPairFacts('owns', ['alice', 'doc.pdf', 'bob', 'other.pdf']);
  pgC.solve();
  const batchAllowAlice = pgC.querySymbol('allow', 'alice');
  const batchAllowBob = pgC.querySymbol('allow', 'bob');
  pgC.freeResult();

  const pgD = await setupBinaryAccess('string_batch_equals_d');
  pgD.addFact('target', 'doc.pdf');
  pgD.addFact2('owns', 'alice', 'doc.pdf');
  pgD.addFact2('owns', 'bob', 'other.pdf');
  pgD.solve();
  if (batchAllowAlice !== pgD.querySymbol('allow', 'alice')) throw new Error('binary alice mismatch');
  if (batchAllowBob !== pgD.querySymbol('allow', 'bob')) throw new Error('binary bob mismatch');
  pgD.freeResult();
});

await test('playground_string_batch_state_guards', async () => {
  const pg = await createPlayground();
  expectThrowRc(() => pg.addRuntimeSymbolFacts('safe', []),
                MAELYS_ERR_INVALID_STATE,
                'empty-state unary string batch');
  expectThrowRc(() => pg.addRuntimeSymbolPairFacts('owns', []),
                MAELYS_ERR_INVALID_STATE,
                'empty-state binary string batch');

  pg.domainBegin('string_batch_state');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('owns', 2, PredKind.EDB);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset('string_batch_state', 'string_batch_state.main', 'allow(X) :- safe(X).\n');
  pg.edbBegin();
  pg.solve();
  expectThrowRc(() => pg.addRuntimeSymbolFacts('safe', []),
                MAELYS_ERR_INVALID_STATE,
                'solved-state unary string batch');
  expectThrowRc(() => pg.addRuntimeSymbolPairFacts('owns', []),
                MAELYS_ERR_INVALID_STATE,
                'solved-state binary string batch');
  pg.freeResult();
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
