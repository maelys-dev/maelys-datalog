import playgroundPkg from '../../js/maelys_playground.js';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

/* Same profile selection as test_build_limits.mjs, so MAELYS_WASM_PROFILE=large
 * really exercises the LARGE dynamic module instead of silently re-testing the
 * SMALL artifact. */
const profile = (process.env.MAELYS_WASM_PROFILE || 'small').toLowerCase();
if (profile !== 'small' && profile !== 'large') {
  throw new Error(`Unsupported MAELYS_WASM_PROFILE="${profile}"`);
}
const buildDir = profile === 'large' ? '../../build/wasm-large' : '../../build/wasm';
const dynamicModule = await import(
  new URL(`${buildDir}/maelys_datalog_dynamic.js`, import.meta.url).href
);
const MaelysDatalogDynamic = dynamicModule.default;

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
  const wasmUrl = new URL(`${buildDir}/maelys_datalog_dynamic.wasm`, import.meta.url).href;
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

const playgroundSource = readFileSync(
  new URL('../../js/maelys_playground.js', import.meta.url),
  'utf8',
);
const decodeEnumeratedTermForTest = runInNewContext(
  `${playgroundSource}\ndecodeEnumeratedTerm;`,
  {},
  { filename: 'maelys_playground.js' },
);

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

await test('playground_derived_fact_count_before_solve_throws', async () => {
  const pg = await createPlayground();
  expectThrowRc(() => pg.derivedFactCount(), -1, 'derivedFactCount before solve');
});

await test('playground_derived_fact_count_known_count', async () => {
  const pg = await createPlayground();
  pg.domainBegin('derived_count_known');
  pg.domainAddPredicate('edge', 2, PredKind.EDB);
  pg.domainAddPredicate('path', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(
    'derived_count_known',
    'derived_count_known.main',
    'path(X, Y) :- edge(X, Y).\npath(X, Z) :- edge(X, Y), path(Y, Z).\n',
  );
  pg.edbBegin();
  pg.addFact2('edge', 'a', 'b');
  pg.addFact2('edge', 'b', 'c');
  pg.solve();
  const count = pg.derivedFactCount();
  if (count !== 3) throw new Error(`expected derived fact count 3, got ${count}`);
  pg.freeResult();
});

await test('playground_derived_fact_count_zero_valid', async () => {
  const pg = await setupUnaryAccess('derived_count_zero');
  pg.solve();
  const count = pg.derivedFactCount();
  if (count !== 0) throw new Error(`expected derived fact count 0, got ${count}`);
  pg.freeResult();
});

await test('playground_enumerate_predicate_facts_symbol_set', async () => {
  const pg = await setupUnaryAccess('enumerate_symbol_set');
  pg.addFact('safe', 'alice');
  pg.addFact('safe', 'bob');
  pg.solve();
  const facts = pg.enumeratePredicateFacts('allow', 1);
  const symbols = new Set(
    facts.map((terms) => {
      if (terms.length !== 1 || terms[0].kind !== 'symbol') {
        throw new Error(`expected unary symbol fact, got ${JSON.stringify(terms)}`);
      }
      return pg.symbolText(terms[0].symbolId);
    }),
  );
  if (facts.length !== 2 || symbols.size !== 2 ||
      !symbols.has('alice') || !symbols.has('bob')) {
    throw new Error(`unexpected enumerated symbols: ${JSON.stringify([...symbols])}`);
  }
  pg.freeResult();
});

await test('playground_enumerate_predicate_facts_empty_query', async () => {
  const pg = await setupUnaryAccess('enumerate_empty_query');
  pg.solve();
  const facts = pg.enumeratePredicateFacts('allow', 1);
  if (!Array.isArray(facts) || facts.length !== 0) {
    throw new Error(`expected [], got ${JSON.stringify(facts)}`);
  }
  pg.freeResult();
});

await test('playground_enumerate_predicate_facts_zero_arity_without_malloc', async () => {
  const pg = await createPlayground();
  pg.domainBegin('enumerate_zero_arity');
  pg.domainAddPredicate('seed', 0, PredKind.POLICY_FACT);
  pg.domainAddPredicate('ready', 0, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(
    'enumerate_zero_arity',
    'enumerate_zero_arity.main',
    'seed().\nready() :- seed().\n',
  );
  pg.edbBegin();
  pg.solve();
  const originalMalloc = pg._mod._malloc;
  let mallocCalls = 0;
  pg._mod._malloc = (bytes) => {
    mallocCalls++;
    return originalMalloc(bytes);
  };
  try {
    const facts = pg.enumeratePredicateFacts('ready', 0);
    if (JSON.stringify(facts) !== '[[]]') {
      throw new Error(`expected one zero-arity fact, got ${JSON.stringify(facts)}`);
    }
    if (mallocCalls !== 0) {
      throw new Error(`zero-arity enumeration called malloc ${mallocCalls} time(s)`);
    }
  } finally {
    pg._mod._malloc = originalMalloc;
    pg.freeResult();
  }
});

await test('playground_enumerate_predicate_facts_symbol_and_max_int', async () => {
  const pg = await createPlayground();
  pg.domainBegin('enumerate_symbol_int');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('mixed', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(
    'enumerate_symbol_int',
    'enumerate_symbol_int.main',
    'mixed(X, 2147483647) :- safe(X).\n',
  );
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.solve();
  const facts = pg.enumeratePredicateFacts('mixed', 2);
  if (facts.length !== 1 || facts[0].length !== 2) {
    throw new Error(`expected one binary fact, got ${JSON.stringify(facts)}`);
  }
  const [symbol, integer] = facts[0];
  if (symbol.kind !== 'symbol' || pg.symbolText(symbol.symbolId) !== 'alice') {
    throw new Error(`unexpected symbol term: ${JSON.stringify(symbol)}`);
  }
  if (integer.kind !== 'int' || integer.text !== '2147483647' ||
      integer.value !== 2147483647) {
    throw new Error(`unexpected integer term: ${JSON.stringify(integer)}`);
  }
  pg.freeResult();
});

await test('playground_decode_int64_synthetic_boundaries', async () => {
  const values = [
    0n,
    -1n,
    2147483647n,
    -2147483648n,
    9007199254740993n,
    -9007199254740993n,
  ];
  for (const expected of values) {
    const encoded = BigInt.asUintN(64, expected);
    const lo = Number(BigInt.asIntN(32, encoded & 0xffffffffn));
    const hi = Number(BigInt.asIntN(32, encoded >> 32n));
    const decoded = decodeEnumeratedTermForTest(2, lo, hi);
    if (decoded.kind !== 'int' || decoded.text !== expected.toString()) {
      throw new Error(`decode mismatch for ${expected}: ${JSON.stringify(decoded)}`);
    }
    const expectedNumber =
      expected >= BigInt(Number.MIN_SAFE_INTEGER) &&
      expected <= BigInt(Number.MAX_SAFE_INTEGER)
        ? Number(expected)
        : null;
    if (decoded.value !== expectedNumber) {
      throw new Error(`number safety mismatch for ${expected}: ${decoded.value}`);
    }
  }
});

await test('playground_enumerate_predicate_facts_rejects_non_query_and_absent', async () => {
  const pg = await createPlayground();
  pg.domainBegin('enumerate_rejections');
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('hidden', 1, PredKind.IDB);
  pg.domainCommit();
  pg.loadRuleset(
    'enumerate_rejections',
    'enumerate_rejections.main',
    'hidden(X) :- safe(X).\n',
  );
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.solve();
  expectThrowRc(
    () => pg.enumeratePredicateFacts('hidden', 1),
    -1,
    'non-query enumeration',
  );
  expectThrowRc(
    () => pg.enumeratePredicateFacts('absent', 1),
    -1,
    'absent predicate enumeration',
  );
  pg.freeResult();
});

await test('playground_symbol_text_before_and_after_solve', async () => {
  const empty = await createPlayground();
  if (empty.symbolText(1) !== null) {
    throw new Error('empty state resolved an invalid symbol id');
  }

  const pg = await setupUnaryAccess('symbol_text_states');
  const emptyId = pg.internRuntimeSymbol('');
  if (pg.symbolText(emptyId) !== '') {
    throw new Error('symbolText confused an interned empty symbol with an invalid id');
  }
  const id = pg.internRuntimeSymbol('visible');
  if (pg.symbolText(id) !== 'visible') {
    throw new Error('symbolText failed before solve');
  }
  if (pg.symbolText(0) !== null || pg.symbolText(id + 1) !== null) {
    throw new Error('symbolText accepted an invalid id');
  }
  pg.addSymbolIdFact('safe', id);
  pg.solve();
  if (pg.symbolText(id) !== 'visible') {
    throw new Error('symbolText failed after solve');
  }
  pg.freeResult();
});

await test('playground_enumeration_guards_and_argument_types', async () => {
  const fresh = await createPlayground();
  expectThrowRc(
    () => fresh.enumeratePredicateFacts('allow', 1),
    -1,
    'enumeration before solve',
  );
  expectThrowType(
    () => fresh.enumeratePredicateFacts(1, 1),
    TypeError,
    'enumeration predicate type',
  );
  expectThrowType(
    () => fresh.enumeratePredicateFacts('allow', 1.5),
    TypeError,
    'enumeration arity integer',
  );
  expectThrowType(
    () => fresh.enumeratePredicateFacts('allow', -1),
    TypeError,
    'enumeration arity non-negative',
  );
  expectThrowType(() => fresh.symbolText(1.5), TypeError, 'symbol id integer');

  const pg = await setupUnaryAccess('enumerate_capacity_guard');
  pg.addFact('safe', 'alice');
  pg.solve();
  const ptr = pg._mod._malloc(4);
  if (!ptr) throw new Error('capacity guard malloc returned 0');
  try {
    const rc = pg._call(
      'maelys_datalog_wasm_enumerate_predicate_facts',
      'number',
      ['string', 'number', 'number', 'number'],
      ['allow', 1, ptr, pg.buildLimits().maxFactsPerPred + 1],
    );
    expectRc(rc, -1, 'enumeration capacity beyond maxFactsPerPred');
  } finally {
    pg._mod._free(ptr);
    pg.freeResult();
  }
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
  /* One fact beyond the per-predicate capacity of the active profile. */
  const overflow = pg.buildLimits().maxFactsPerPred + 1;
  const values = Array.from({ length: overflow }, (_, i) => `user_${i}`);
  expectThrowRc(() => pg.addRuntimeSymbolFacts('safe', values),
                MAELYS_ERR_PAYLOAD_TOO_LARGE,
                'per-predicate capacity');
  pg.solve();
  if (pg.querySymbol('allow', 'user_0')) throw new Error('capacity failure inserted user_0');
  if (pg.querySymbol('allow', `user_${overflow - 1}`)) {
    throw new Error(`capacity failure inserted user_${overflow - 1}`);
  }
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

/* ====================================================================
 * P4-C66 — Why-true text through the real dynamic module.
 * ==================================================================== */

const WHY_TRUE_ALLOW_ALICE =
  'MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n' +
  'status=complete\n' +
  'steps=1 premises=1\n' +
  'step=0 rule=1 fact="allow"("alice")\n' +
  'premise=0 body=0 kind=positive origin=edb fact="safe"("alice") parent=-\n' +
  'result-step=0\n';

const WHY_TRUE_PATH_AB =
  'MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n' +
  'status=complete\n' +
  'steps=1 premises=1\n' +
  'step=0 rule=1 fact="path"("a","b")\n' +
  'premise=0 body=0 kind=positive origin=edb fact="edge"("a","b") parent=-\n' +
  'result-step=0\n';

const WHY_TRUE_PATH_AC =
  'MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n' +
  'status=complete\n' +
  'steps=2 premises=3\n' +
  'step=0 rule=1 fact="path"("a","b")\n' +
  'premise=0 body=0 kind=positive origin=edb fact="edge"("a","b") parent=-\n' +
  'step=1 rule=2 fact="path"("a","c")\n' +
  'premise=1 body=0 kind=positive origin=idb fact="path"("a","b") parent=0\n' +
  'premise=2 body=1 kind=positive origin=edb fact="edge"("b","c") parent=-\n' +
  'result-step=1\n';

const WHY_TRUE_TRUNCATED =
  'MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n' +
  'status=truncated\n' +
  'steps=0 premises=0\n';

function expectText(actual, expected, label) {
  if (actual !== expected) {
    throw new Error(`${label}: text mismatch\n--- expected ---\n${expected}--- got ---\n${actual}`);
  }
}

/* safe(alice) plus an EDB-only queryable predicate. */
async function setupWhyTrueUnary(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('safe', 1, PredKind.EDB);
  pg.domainAddPredicate('observed', 1, PredKind.EDB | PredKind.QUERY);
  pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
  pg.domainAddPredicate('internal', 1, PredKind.IDB);
  pg.domainCommit();
  pg.loadRuleset(
    domainName,
    `${domainName}.main`,
    'allow(X) :- safe(X).\ninternal(X) :- safe(X).\n',
  );
  pg.edbBegin();
  pg.addFact('safe', 'alice');
  pg.addFact('observed', 'alice');
  pg.solve();
  return pg;
}

/* Transitive closure over edge(a,b), edge(b,c). */
async function setupWhyTrueBinary(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('edge', 2, PredKind.EDB);
  pg.domainAddPredicate('path', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(
    domainName,
    `${domainName}.main`,
    'path(X, Y) :- edge(X, Y).\npath(X, Z) :- path(X, Y), edge(Y, Z).\n',
  );
  pg.edbBegin();
  pg.addFact2('edge', 'a', 'b');
  pg.addFact2('edge', 'b', 'c');
  pg.solve();
  return pg;
}

/* Public truncation construction: 9 nodes give 36 path + 36 reach facts,
 * beyond the 64 proof nodes the solver retains. No internal state is forged. */
async function setupWhyTrueTruncated(domainName) {
  const pg = await createPlayground();
  pg.domainBegin(domainName);
  pg.domainAddPredicate('edge', 2, PredKind.EDB);
  pg.domainAddPredicate('path', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainAddPredicate('reach', 2, PredKind.IDB | PredKind.QUERY);
  pg.domainCommit();
  pg.loadRuleset(
    domainName,
    `${domainName}.main`,
    'path(X, Y) :- edge(X, Y).\npath(X, Z) :- path(X, Y), edge(Y, Z).\nreach(X, Y) :- path(X, Y).\n',
  );
  pg.edbBegin();
  for (let i = 0; i < 8; i++) pg.addFact2('edge', `n${i}`, `n${i + 1}`);
  pg.solve();
  return pg;
}

function instrumentAllocations(pg) {
  const originalMalloc = pg._mod._malloc;
  const originalFree = pg._mod._free;
  const stats = {
    allocated: 0,
    freed: 0,
    restore() {
      pg._mod._malloc = originalMalloc;
      pg._mod._free = originalFree;
    },
  };
  pg._mod._malloc = (bytes) => {
    const ptr = originalMalloc(bytes);
    if (ptr) stats.allocated++;
    return ptr;
  };
  pg._mod._free = (ptr) => {
    if (ptr) stats.freed++;
    return originalFree(ptr);
  };
  return stats;
}

function heapBytes(pg) {
  return pg._mod.HEAP32.buffer.byteLength;
}

await test('playground_explain_fact_text_validates_types_without_wasm', async () => {
  const pg = await setupWhyTrueUnary('why_true_types');
  const originalCcall = pg._mod.ccall;
  let calls = 0;
  pg._mod.ccall = (...args) => {
    calls++;
    return originalCcall.apply(pg._mod, args);
  };
  try {
    expectThrowType(() => pg.explainFactText(42, ['alice']), TypeError, 'predicate type');
    expectThrowType(() => pg.explainFactText('allow', 'alice'), TypeError, 'terms not an array');
    expectThrowType(() => pg.explainFactText('allow', []), TypeError, 'zero arity');
    expectThrowType(() => pg.explainFactText('allow', ['a', 'b', 'c']), TypeError, 'arity three');
    expectThrowType(() => pg.explainFactText('allow', [1]), TypeError, 'non-string term');
    expectThrowType(() => pg.explainFactText('allow', [null]), TypeError, 'null term');
    expectThrowType(() => pg.explainFactText('allow', ['a', 7]), TypeError, 'non-string second term');
    if (calls !== 0) throw new Error(`local validation called the module ${calls} time(s)`);
  } finally {
    pg._mod.ccall = originalCcall;
    pg.freeResult();
  }
});

await test('playground_explain_fact_text_arity1_golden', async () => {
  const pg = await setupWhyTrueUnary('why_true_arity1');
  expectText(pg.explainFactText('allow', ['alice']), WHY_TRUE_ALLOW_ALICE, 'arity 1 golden');
  pg.freeResult();
});

await test('playground_explain_fact_text_arity2_golden', async () => {
  const pg = await setupWhyTrueBinary('why_true_arity2');
  expectText(pg.explainFactText('path', ['a', 'b']), WHY_TRUE_PATH_AB, 'arity 2 golden');
  expectText(pg.explainFactText('path', ['a', 'c']), WHY_TRUE_PATH_AC, 'arity 2 two-step golden');
  pg.freeResult();
});

await test('playground_explain_fact_text_two_fresh_instances_agree', async () => {
  const pgA = await setupWhyTrueBinary('why_true_instance_a');
  const pgB = await setupWhyTrueBinary('why_true_instance_b');
  const textA = pgA.explainFactText('path', ['a', 'c']);
  const textB = pgB.explainFactText('path', ['a', 'c']);
  expectText(textA, WHY_TRUE_PATH_AC, 'first instance');
  expectText(textB, textA, 'second instance');
  pgA.freeResult();
  pgB.freeResult();
});

await test('playground_explain_fact_text_absent_fact_is_null', async () => {
  const pg = await setupWhyTrueBinary('why_true_absent');
  if (pg.querySymbol2('path', 'c', 'a')) throw new Error('path(c,a) unexpectedly present');
  if (pg.explainFactText('path', ['c', 'a']) !== null) {
    throw new Error('absent fact must be null');
  }
  pg.freeResult();
});

await test('playground_explain_fact_text_unknown_term_is_null_without_interning', async () => {
  const pg = await setupWhyTrueUnary('why_true_unknown_term');
  const symbolsBefore = [];
  for (let id = 1; ; id++) {
    const text = pg.symbolText(id);
    if (text === null) break;
    symbolsBefore.push(text);
  }
  if (pg.explainFactText('allow', ['ghost']) !== null) {
    throw new Error('unknown term must be null');
  }
  const symbolsAfter = [];
  for (let id = 1; ; id++) {
    const text = pg.symbolText(id);
    if (text === null) break;
    symbolsAfter.push(text);
  }
  if (symbolsAfter.length !== symbolsBefore.length) {
    throw new Error(`symbol table grew from ${symbolsBefore.length} to ${symbolsAfter.length}`);
  }
  if (symbolsAfter.includes('ghost')) throw new Error('ghost was interned');
  pg.freeResult();
});

await test('playground_explain_fact_text_edb_only_fact_is_null', async () => {
  const pg = await setupWhyTrueUnary('why_true_edb_only');
  /* querySymbol answers true for an EDB atom, yet there is no derivation to
   * explain. The POLICY_FACT counterpart needs a registered atom, which the
   * WASM domain builder cannot declare; it is covered by the C boundary test. */
  if (!pg.querySymbol('observed', 'alice')) throw new Error('observed(alice) missing');
  if (pg.explainFactText('observed', ['alice']) !== null) {
    throw new Error('EDB-only fact must be null');
  }
  pg.freeResult();
});

await test('playground_explain_fact_text_predicate_errors_throw', async () => {
  const pg = await setupWhyTrueUnary('why_true_predicate_errors');
  expectThrowRc(() => pg.explainFactText('missing', ['alice']),
                MAELYS_ERR_INVALID_FIELD,
                'absent predicate');
  /* An unknown term never masks a predicate error. */
  expectThrowRc(() => pg.explainFactText('missing', ['ghost']),
                MAELYS_ERR_INVALID_FIELD,
                'absent predicate with unknown term');
  expectThrowRc(() => pg.explainFactText('internal', ['alice']),
                MAELYS_ERR_INVALID_FIELD,
                'non-QUERY predicate');
  expectThrowRc(() => pg.explainFactText('allow', ['alice', 'bob']),
                MAELYS_ERR_INVALID_FIELD,
                'wrong arity');
  pg.freeResult();
});

await test('playground_explain_fact_text_state_guards_throw', async () => {
  const pg = await createPlayground();
  expectThrowRc(() => pg.explainFactText('allow', ['alice']),
                MAELYS_ERR_INVALID_STATE,
                'before any domain');

  const solved = await setupWhyTrueUnary('why_true_state_guards');
  expectText(solved.explainFactText('allow', ['alice']), WHY_TRUE_ALLOW_ALICE, 'solved');
  solved.freeResult();
  expectThrowRc(() => solved.explainFactText('allow', ['alice']),
                MAELYS_ERR_INVALID_STATE,
                'after freeResult');
});

await test('playground_explain_fact_text_truncated_is_a_string', async () => {
  const pg = await setupWhyTrueTruncated('why_true_truncated');
  const derived = pg.derivedFactCount();
  if (derived !== 72) throw new Error(`expected 72 derived facts, got ${derived}`);
  if (!pg.querySymbol2('path', 'n0', 'n8')) throw new Error('path(n0,n8) missing');
  const text = pg.explainFactText('path', ['n0', 'n8']);
  if (text === null) throw new Error('a bounded provenance must not be null');
  expectText(text, WHY_TRUE_TRUNCATED, 'truncated');
  if (pg.explainFactText('path', ['n8', 'n0']) !== null) {
    throw new Error('absent fact must stay null');
  }
  pg.freeResult();
});

await test('playground_explain_fact_text_or_matches_manual_expansion', async () => {
  const buildDisjunction = async (domainName, src) => {
    const pg = await createPlayground();
    pg.domainBegin(domainName);
    pg.domainAddPredicate('edge', 2, PredKind.EDB);
    pg.domainAddPredicate('link', 2, PredKind.EDB);
    pg.domainAddPredicate('path', 2, PredKind.IDB | PredKind.QUERY);
    pg.domainCommit();
    pg.loadRuleset(domainName, `${domainName}.main`, src);
    pg.edbBegin();
    pg.addFact2('link', 'alice', 'doc');
    pg.solve();
    return pg;
  };
  const pgOr = await buildDisjunction('why_true_or',
                                      'path(X, Y) :- edge(X, Y) or link(X, Y).\n');
  const pgManual = await buildDisjunction('why_true_manual',
                                          'path(X, Y) :- edge(X, Y).\npath(X, Y) :- link(X, Y).\n');
  const orText = pgOr.explainFactText('path', ['alice', 'doc']);
  const manualText = pgManual.explainFactText('path', ['alice', 'doc']);
  if (orText === null) throw new Error('disjunction produced no explanation');
  expectText(orText, manualText, 'or vs manual expansion');
  pgOr.freeResult();
  pgManual.freeResult();
});

await test('playground_explain_fact_text_count_and_write_agree', async () => {
  const pg = await setupWhyTrueUnary('why_true_count_write');
  const scalars = pg._mod._malloc(8);
  if (!scalars) throw new Error('scalar malloc returned 0');
  const requiredPtr = scalars;
  const foundPtr = scalars + 4;
  const call = (ptr, capacity) => pg._call(
    'maelys_datalog_wasm_explain_symbol_fact_text',
    'number',
    ['string', 'string', 'number', 'number', 'number', 'number'],
    ['allow', 'alice', ptr, capacity, requiredPtr, foundPtr],
  );
  let textPtr = 0;
  try {
    expectRc(call(0, 0), 0, 'count-only rc');
    const required = pg._mod.getValue(requiredPtr, 'i32');
    const found = pg._mod.getValue(foundPtr, 'i32');
    if (found !== 1) throw new Error(`expected found=1, got ${found}`);
    if (required !== pg._mod.lengthBytesUTF8(WHY_TRUE_ALLOW_ALICE)) {
      throw new Error(`count-only size ${required} does not match the golden text`);
    }

    /* Exactly required bytes leaves no room for the terminator. */
    textPtr = pg._mod._malloc(required + 1);
    if (!textPtr) throw new Error('text malloc returned 0');
    pg._mod.HEAP32[requiredPtr >> 2] = 0;
    pg._mod.HEAP32[foundPtr >> 2] = 0;
    expectRc(call(textPtr, required), MAELYS_ERR_PAYLOAD_TOO_LARGE, 'capacity == required');
    if (pg._mod.getValue(foundPtr, 'i32') !== 1) throw new Error('found lost on tight capacity');
    if (pg._mod.getValue(requiredPtr, 'i32') !== required) {
      throw new Error('size lost on tight capacity');
    }
    if (pg._mod.UTF8ToString(textPtr) !== '') throw new Error('tight capacity left a prefix');

    pg._mod.HEAP32[requiredPtr >> 2] = 0;
    pg._mod.HEAP32[foundPtr >> 2] = 0;
    expectRc(call(textPtr, required + 1), 0, 'capacity == required + 1');
    if (pg._mod.getValue(requiredPtr, 'i32') !== required) {
      throw new Error('write size diverged from the counted size');
    }
    expectText(pg._mod.UTF8ToString(textPtr), WHY_TRUE_ALLOW_ALICE, 'raw write');
  } finally {
    if (textPtr) pg._mod._free(textPtr);
    pg._mod._free(scalars);
    pg.freeResult();
  }
});

await test('playground_explain_fact_text_malloc_failure_is_clean', async () => {
  const pg = await setupWhyTrueUnary('why_true_malloc_zero');
  const originalMalloc = pg._mod._malloc;
  const originalFree = pg._mod._free;

  /* The scalar allocation fails: nothing is allocated, nothing is freed. */
  let frees = 0;
  pg._mod._malloc = () => 0;
  pg._mod._free = (ptr) => { frees++; return originalFree(ptr); };
  try {
    expectThrowType(() => pg.explainFactText('allow', ['alice']), Error, 'scalar malloc zero');
    if (frees !== 0) throw new Error(`unexpected free after a failed allocation (${frees})`);
  } finally {
    pg._mod._malloc = originalMalloc;
    pg._mod._free = originalFree;
  }

  /* The text allocation fails: the scalar block is still released. */
  let allocations = 0;
  frees = 0;
  pg._mod._malloc = (bytes) => {
    allocations++;
    return allocations === 1 ? originalMalloc(bytes) : 0;
  };
  pg._mod._free = (ptr) => { frees++; return originalFree(ptr); };
  try {
    expectThrowType(() => pg.explainFactText('allow', ['alice']), Error, 'text malloc zero');
    if (allocations !== 2) throw new Error(`expected 2 allocation attempts, got ${allocations}`);
    if (frees !== 1) throw new Error(`expected the scalar block to be freed once, got ${frees}`);
  } finally {
    pg._mod._malloc = originalMalloc;
    pg._mod._free = originalFree;
    pg.freeResult();
  }
});

await test('playground_explain_fact_text_allocations_are_balanced', async () => {
  const pg = await setupWhyTrueUnary('why_true_alloc_balance');
  const stats = instrumentAllocations(pg);
  try {
    expectText(pg.explainFactText('allow', ['alice']), WHY_TRUE_ALLOW_ALICE, 'success path');
    if (stats.allocated !== 2 || stats.freed !== 2) {
      throw new Error(`success path: ${stats.allocated} allocated, ${stats.freed} freed`);
    }

    stats.allocated = 0;
    stats.freed = 0;
    if (pg.explainFactText('allow', ['ghost']) !== null) throw new Error('expected null');
    if (stats.allocated !== 1 || stats.freed !== 1) {
      throw new Error(`absence path: ${stats.allocated} allocated, ${stats.freed} freed`);
    }

    stats.allocated = 0;
    stats.freed = 0;
    expectThrowRc(() => pg.explainFactText('internal', ['alice']),
                  MAELYS_ERR_INVALID_FIELD,
                  'error path');
    if (stats.allocated !== 1 || stats.freed !== 1) {
      throw new Error(`error path: ${stats.allocated} allocated, ${stats.freed} freed`);
    }
  } finally {
    stats.restore();
    pg.freeResult();
  }
});

await test('playground_explain_fact_text_is_never_called_implicitly', async () => {
  const pg = await createPlayground();
  const originalCcall = pg._mod.ccall;
  const names = [];
  pg._mod.ccall = (name, ...rest) => {
    names.push(name);
    return originalCcall.call(pg._mod, name, ...rest);
  };
  try {
    pg.domainBegin('why_true_no_implicit');
    pg.domainAddPredicate('safe', 1, PredKind.EDB);
    pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
    pg.domainCommit();
    pg.loadRuleset('why_true_no_implicit', 'why_true_no_implicit.main', 'allow(X) :- safe(X).\n');
    pg.edbBegin();
    pg.addFact('safe', 'alice');
    pg.solve();
    pg.querySymbol('allow', 'alice');
    pg.derivedFactCount();
    pg.enumeratePredicateFacts('allow', 1);
    pg.freeResult();
    const leaked = names.filter((name) => name.includes('explain'));
    if (leaked.length !== 0) {
      throw new Error(`implicit explanation calls: ${leaked.join(', ')}`);
    }
  } finally {
    pg._mod.ccall = originalCcall;
  }
});

await test('playground_explain_fact_text_memory_is_stable_across_repetitions', async () => {
  const complete = await setupWhyTrueUnary('why_true_memory_complete');
  const beforeFirst = heapBytes(complete);
  expectText(complete.explainFactText('allow', ['alice']), WHY_TRUE_ALLOW_ALICE, 'warmup');
  const afterFirst = heapBytes(complete);
  for (let i = 0; i < 16; i++) complete.explainFactText('allow', ['alice']);
  const afterRepeat = heapBytes(complete);
  if (afterRepeat !== afterFirst) {
    throw new Error(`complete: memory grew from ${afterFirst} to ${afterRepeat} on repetition`);
  }
  console.log(`      complete: ${beforeFirst} -> ${afterFirst} -> ${afterRepeat} bytes`);
  complete.freeResult();

  const truncated = await setupWhyTrueTruncated('why_true_memory_truncated');
  const beforeTruncated = heapBytes(truncated);
  truncated.explainFactText('path', ['n0', 'n8']);
  const afterTruncated = heapBytes(truncated);
  for (let i = 0; i < 16; i++) truncated.explainFactText('path', ['n0', 'n8']);
  const afterTruncatedRepeat = heapBytes(truncated);
  if (afterTruncatedRepeat !== afterTruncated) {
    throw new Error(
      `truncated: memory grew from ${afterTruncated} to ${afterTruncatedRepeat} on repetition`,
    );
  }
  console.log(
    `      truncated: ${beforeTruncated} -> ${afterTruncated} -> ${afterTruncatedRepeat} bytes`,
  );
  truncated.freeResult();
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
