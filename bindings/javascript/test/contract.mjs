/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';

const packageRoot = resolve(process.env.MAELYS_JS_PACKAGE || 'build/javascript-package');
const profile = process.env.MAELYS_PROFILE || 'small';
const runtimes = (process.env.MAELYS_JS_RUNTIMES || 'node,wasm').split(',');
const f = (predicate, ...terms) => ({ predicate, terms });
const documents = new Map();
for (const runtime of runtimes) {
  const api = await import(pathToFileURL(resolve(packageRoot, `src/${runtime}.mjs`)));
  const { Engine, Predicate: P, Status: S, MaelysDatalogError, Capability, ExplanationKind } = api;
  const status = expected => error => error instanceof MaelysDatalogError && error.status === expected;
  async function basic(t, options) {
    const engine = await Engine.create({ profile }); t.after(() => engine.close());
    engine.registerDomain('js_common', [P.edbQuery('seed', 1), P.edb('blocked', 1), P.idbQuery('allow', 1), P.idb('hidden', 1)]);
    const rules = engine.loadInlineRuleset('js_common', 'test', 'allow(X) :- seed(X), not(blocked(X)).');
    return { engine, rules, edb: rules.edb(options) };
  }
  test(`${runtime}: exact values, copied inputs and independently owned objects`, async t => {
    const { engine, rules, edb } = await basic(t);
    const values = [-(1n << 63n), (1n << 63n) - 1n, 9007199254740993n, 0n, 1, true, false, '1', '', 'é🙂'];
    const batch = values.map(v => f('seed', v)); edb.addFacts(batch); batch[0].terms[0] = 'mutated';
    const other = rules.edb(); other.addFact('seed', ['independent']);
    const a = rules.solve(edb), b = rules.solve(other);
    for (const value of values) assert.equal(a.containsFact('allow', [value]), true);
    assert.equal(b.containsFact('allow', ['independent']), true);
    assert.equal(a.derivedFactCount(), values.length);
    const copied = a.enumeratePredicateFacts('allow', 1).flat();
    assert.deepEqual(new Set(copied), new Set(values.map(v => typeof v === 'number' ? BigInt(v) : v)));
    a.close(); assert.equal(b.containsFact('allow', ['independent']), true);
    engine.close(); engine.close(); assert.ok(copied.includes('é🙂'));
    assert.throws(() => b.derivedFactCount(), status(S.INVALID_STATE));
  });
  test(`${runtime}: result leases, reset snapshots, cascading close and foreign terms`, async t => {
    const { rules, edb } = await basic(t);
    edb.addFact('seed', ['alice']);
    const session = rules.prepare(), a = session.solve(edb);
    const raw = a.enumerateRaw('allow', 1)[0][0];
    assert.equal(raw.kind, 'symbol'); assert.equal(raw.resolve(), 'alice'); assert.ok(Object.isFrozen(raw));
    assert.throws(() => session.solve(edb), status(S.INVALID_STATE));
    assert.throws(() => edb.addFacts([]), status(S.INVALID_STATE));
    assert.throws(() => edb.clear(), status(S.INVALID_STATE));
    edb.reset(); edb.addFact('seed', ['bob']);
    const b = rules.solve(edb);
    assert.equal(a.containsFact('allow', ['alice']), true);
    assert.equal(b.containsFact('allow', ['bob']), true);
    assert.throws(() => b.resolveTerm(raw), TypeError);
    a.close(); a.close(); assert.throws(() => raw.resolve(), status(S.INVALID_STATE));
    const again = session.solve(edb); assert.equal(again.containsFact('allow', ['bob']), true);
    rules.close(); rules.close();
    for (const object of [session, edb, again, b]) object.close();
    assert.throws(() => session.capacities, status(S.INVALID_STATE));
  });
  test(`${runtime}: foreign engines and rulesets rejected before crossing C ownership`, async t => {
    const a = await basic(t), b = await basic(t);
    const second = a.engine.loadInlineRuleset('js_common', 'other', 'allow(X) :- seed(X).');
    assert.throws(() => a.rules.solve(b.edb), TypeError);
    assert.throws(() => a.rules.prepare().solve(second.edb()), TypeError);
    assert.throws(() => a.rules.solve({}), TypeError);
    assert.throws(() => new api.Ruleset(), TypeError);
  });
  test(`${runtime}: bad values and failed native append leave the entire EDB unchanged`, async t => {
    const { edb } = await basic(t, { factCapacity: 4, textCapacity: 64 });
    edb.addFact('seed', ['keep']); const before = edb.usage;
    for (const bad of [NaN, Infinity, 1.5, 9007199254740992, 1n << 63n, -(1n << 63n) - 1n, null, {}, 'x\0y', '\ud800', '\udc00']) {
      assert.throws(() => edb.addFacts([f('seed', 'new'), f('seed', bad)]));
      assert.deepEqual(edb.usage, before);
    }
    assert.throws(() => edb.addFacts([f('seed', 'new'), f('seed', 'x'.repeat(80))]), status(S.PAYLOAD_TOO_LARGE));
    assert.deepEqual(edb.usage, before);
    let read = 0;
    function* unbounded() { while (true) { ++read; yield f('seed', 1); } }
    assert.throws(() => edb.addFacts(unbounded()), RangeError);
    assert.equal(read, 4); assert.deepEqual(edb.usage, before);
  });
  test(`${runtime}: reentrant iterables cannot reset or solve a partially staged batch`, async t => {
    const { edb, rules } = await basic(t); edb.addFact('seed', ['keep']);
    const session = rules.prepare();
    function* reentrant() { yield f('seed', 'new'); session.solve(edb); }
    assert.throws(() => edb.addFacts(reentrant()), status(S.INVALID_STATE)); assert.equal(edb.count, 1);
    function* resets() { yield f('seed', 'new'); edb.reset(); }
    assert.throws(() => edb.addFacts(resets()), status(S.INVALID_STATE)); assert.equal(edb.count, 1);
    function* closes() { yield f('seed', 'new'); rules.close(); }
    assert.throws(() => edb.addFacts(closes()), status(S.INVALID_STATE));
  });
  test(`${runtime}: failed solve is retryable and diagnostics are immutable snapshots`, async t => {
    const { engine, rules, edb } = await basic(t);
    let saved;
    assert.throws(() => engine.loadInlineRuleset('js_common', 'bad', 'allow(X) :- seed(Y).'), error => {
      saved = error.diagnostic; return error.status !== 0 && Boolean(saved.message);
    });
    const text = saved.message;
    edb.addFact('missing', ['alice']); assert.throws(() => rules.solve(edb)); assert.equal(edb.count, 1);
    edb.clear(); edb.addFacts([f('seed', 'alice'), f('seed', 'bob'), f('blocked', 'bob')]);
    const result = rules.solve(edb);
    assert.equal(result.containsFact('allow', ['never-interned']), false);
    for (const method of ['explainTrue', 'explainFalse']) assert.throws(() => result[method]('allow', ['never-interned']), status(S.NOT_FOUND));
    assert.throws(() => result.containsFact('blocked', ['bob']), status(S.INVALID_FIELD));
    assert.throws(() => result.enumerateRaw('hidden', 1), status(S.INVALID_FIELD));
    assert.equal(saved.message, text); assert.ok(Object.isFrozen(saved));
  });
  test(`${runtime}: explicit session capacities, capability admission and fingerprints`, async t => {
    const { rules, edb, engine } = await basic(t);
    const s = rules.prepare({ capacities: { inputFacts: 1, derivedFacts: 1, symbols: 8, textBytes: 64 }, explanations: ExplanationKind.TRUE | ExplanationKind.FALSE });
    assert.deepEqual({ ...s.capacities }, { inputFacts: 1, derivedFacts: 1, symbols: 8, textBytes: 64 });
    for (const fp of [rules.fingerprint, s.fingerprint, s.executionFingerprint]) assert.match(fp, /^[0-9a-f]{64}$/);
    edb.addFact('seed', ['alice']); const result = s.solve(edb);
    assert.equal(result.fingerprint, s.fingerprint);
    assert.equal(result.executionFingerprint, s.executionFingerprint);
    const document = result.explainTrue('allow', ['alice']);
    assert.equal(result.explainTrue('allow', ['alice']), document);
    assert.throws(() => rules.prepare({ workLimit: 1n }), status(S.UNSUPPORTED));
    assert.throws(() => rules.prepare({ requiredCapabilities: Capability.WORK_LIMIT }), status(S.UNSUPPORTED));
    assert.throws(() => rules.prepare({ explanations: 4 }), status(S.INVALID_ARGUMENT));
    assert.throws(() => rules.prepare({ capacities: { inputFacts: -1 } }), RangeError);
    const zero = rules.prepare({ capacities: { symbols: 0 } });
    assert.equal(zero.capacities.symbols, 0);
    assert.throws(() => zero.solve(edb), status(S.PAYLOAD_TOO_LARGE));
    const defaults = rules.prepare().capacities;
    assert.equal(defaults.inputFacts, engine.limits.maxEdbFacts);
  });
  test(`${runtime}: in-memory manifests verify hashes, flags and policy selection`, async t => {
    const { engine } = await basic(t);
    const sources = ['allow(X) :- seed(X).', 'allow(X) :- seed(X), not(blocked(X)).'];
    const policies = sources.map((source, i) => ({ policy_id: `policy-${i}`, domain: 'js_common', file: `policy-${i}.dl`,
      sha256: createHash('sha256').update(source).digest('hex'), mode: 'enforce', enabled: true,
      description: 'JS shared contract', queries: [{ name: 'allow', arity: 1 }] }));
    const manifest = { policy_set_id: 'js.manifest', policy_set_version: '1', manifest_version: '1',
      default_profile: 'enforce', created_for: 'test', strict_loading: true, fail_closed: true, capabilities: [], policies };
    const bundle = sources.map((source, i) => ({ policyId: `policy-${i}`, source }));
    const load = () => engine.loadManifest({ text: JSON.stringify(manifest), policies: bundle });
    const rules = load(); assert.equal(rules.policyCount, 2);
    const edb = rules.edb(); edb.addFacts([f('seed', 'bob'), f('blocked', 'bob')]);
    assert.equal(rules.solve(edb, { policyIndex: 0 }).containsFact('allow', ['bob']), true);
    assert.equal(rules.solve(edb, { policyIndex: 1 }).containsFact('allow', ['bob']), false);
    assert.throws(() => rules.prepare({ policyIndex: 2 }));
    bundle[0].source += '\n'; assert.throws(load); bundle[0].source = sources[0];
    policies[0].mode = 'test_only'; assert.throws(load, status(S.FORBIDDEN));
    const admitted = engine.loadManifest({ text: JSON.stringify(manifest), policies: bundle }, { allowTestOnly: true });
    assert.equal(admitted.policyCount, 2);
    policies[0].mode = 'enforce';
    const dir = await mkdtemp(resolve(tmpdir(), 'maelys-js-manifest-')); t.after(() => rm(dir, { recursive: true, force: true }));
    await Promise.all(sources.map((source, i) => writeFile(resolve(dir, `policy-${i}.dl`), source)));
    const path = resolve(dir, 'manifest.json'); await writeFile(path, JSON.stringify(manifest));
    if (runtime === 'node') assert.equal(engine.loadManifest({ path }).fingerprint, rules.fingerprint);
    else assert.throws(() => engine.loadManifest({ path }), status(S.UNSUPPORTED));
  });
  test(`${runtime}: canonical explanations match across runtimes and workspace modes`, async t => {
    const { rules, edb } = await basic(t);
    edb.addFacts([f('seed', 'alice'), f('seed', 'bob'), f('blocked', 'bob')]);
    const a = rules.solve(edb), b = rules.solve(edb, { explanations: 3 });
    const actual = [a.explainTrue('allow', ['alice']), a.explainFalse('allow', ['bob']), a.explainTrue('allow', ['bob']), a.explainFalse('allow', ['alice'])];
    assert.deepEqual(actual, [b.explainTrue('allow', ['alice']), b.explainFalse('allow', ['bob']), b.explainTrue('allow', ['bob']), b.explainFalse('allow', ['alice'])]);
    if (documents.has(profile)) assert.deepEqual(actual, documents.get(profile));
    documents.set(profile, actual);
    assert.match(actual[0], /status=complete/); assert.match(actual[1], /status=complete/);
    assert.match(actual[2], /status=not-derived/); assert.match(actual[3], /status=not-applicable/);
  });
  test(`${runtime}: arity zero through four and typed aggregate behavior`, async t => {
    const engine = await Engine.create({ profile }); t.after(() => engine.close());
    engine.registerDomain('js_arity', Array.from({ length: 5 }, (_, n) => [P.edb(`e${n}`, n), P.idbQuery(`q${n}`, n)]).flat());
    const source = Array.from({ length: 5 }, (_, n) => {
      const terms = ['A','B','C','D'].slice(0,n).join(','); return `q${n}(${terms}) :- e${n}(${terms}).`;
    }).join(' ');
    const rules = engine.loadInlineRuleset('js_arity','main',source), edb = rules.edb();
    const values = ['🙂', (1n << 63n) - 1n, true, ''];
    edb.addFacts(Array.from({ length: 5 }, (_, n) => f(`e${n}`, ...values.slice(0,n))));
    const result = rules.solve(edb);
    for(let n=0;n<5;++n) assert.deepEqual(result.enumeratePredicateFacts(`q${n}`,n),[values.slice(0,n)]);
    engine.registerDomain('js_aggregates',[P.edb('group',1),P.edb('event',3),...['n','lo','hi','total'].map(name=>P.idbQuery(name,2))]);
    const aggregateRules = engine.loadInlineRuleset('js_aggregates','main',
      'n(G,N) :- group(G),count(V,event(_,G,V),N). lo(G,N) :- group(G),min(V,event(_,G,V),N). hi(G,N) :- group(G),max(V,event(_,G,V),N). total(G,N) :- group(G),sum(V,event(_,G,V),N).');
    const input=aggregateRules.edb(); input.addFacts([f('group','api'),f('group','empty'),f('event',1,'api',5),f('event',2,'api',5),f('event',3,'api',9),f('event',2,'api',5)]);
    const aggregates=aggregateRules.solve(input);
    for(const [name,value] of [['n',2n],['lo',5n],['hi',9n],['total',19n]]) assert.equal(aggregates.containsFact(name,['api',value]),true);
    assert.equal(aggregates.containsFact('total',['empty',0]),true);
    input.reset(); input.addFacts([f('group','api'),f('event',1,'api',2147483647),f('event',2,'api',1)]);
    assert.throws(()=>aggregateRules.solve(input), error=>error.status===S.INVALID_FIELD && error.diagnostic.codeName==='solve_sum_overflow' && error.diagnostic.aggregate.value==='2147483648');
    assert.equal(aggregates.containsFact('total',['api',19]),true);
  });

}
