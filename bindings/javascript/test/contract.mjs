/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';
import { mkdtemp, writeFile, rm, readFile } from 'node:fs/promises';
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
  test(`${runtime}: optional allocator service is not exposed by the binding`, async t => {
    const {rules}=await basic(t);
    const fixed=rules.prepare(), identity=fixed.executionFingerprint;
    const options={memoryMode:1,executionByteCap:1024,
      get allocator(){throw new Error('allocator must not be read');}};
    assert.deepEqual(Object.keys(new api.SessionCapacities(options)),[]);
    const session=rules.prepare(options);
    assert.equal(session.executionFingerprint,identity);
    assert.equal('allocationStats' in session,false);
    session.close();fixed.close();
  });
  test(`${runtime}: retained input exact values, bases, leases and set semantics`, async t => {
    const { rules, edb } = await basic(t), session=rules.prepare({explanations:3});
    const fingerprint=session.executionFingerprint;
    const inputs=session.inputs({factCapacity:16,symbols:['','1','é🙂']});
    const base=inputs.base;
    assert.ok(base instanceof api.InputBase && Object.isFrozen(base));
    assert.notEqual(session.executionFingerprint,fingerprint);
    const values=[-(1n<<63n),(1n<<63n)-1n,9007199254740993n,true,false,'','1','é🙂'];
    let result=inputs.replace(base,values.map(v=>f('seed',v)));
    for(const v of values) assert.equal(result.containsFact('allow',[v]),true);
    assert.equal(result.derivedFactCount(),values.length);
    assert.match(result.explainTrue('allow',['é🙂']),/status=complete/);
    assert.throws(()=>inputs.close(),status(S.INVALID_STATE));
    assert.throws(()=>inputs.apply(inputs.base),status(S.INVALID_STATE));
    assert.throws(()=>session.solve(edb),status(S.INVALID_STATE)); result.close();
    assert.deepEqual(inputs.base,new api.InputBase(base.incarnation,base.generation+1n));
    assert.throws(()=>inputs.apply(base),status(S.INVALID_STATE));
    const current=inputs.base;
    for(const forged of [new api.InputBase(current.incarnation,current.generation+(1n<<32n)),new api.InputBase(current.incarnation+(1n<<32n),current.generation)]){
      assert.throws(()=>inputs.apply(forged),status(S.INVALID_STATE));assert.deepEqual(inputs.base,current);
    }
    result=inputs.apply(inputs.base,{added:[f('seed','é🙂'),f('seed','é🙂')],removed:[f('seed','é🙂'),f('seed','unknown')]});
    assert.equal(result.containsFact('allow',['é🙂']),true); result.close();
    result=inputs.apply(inputs.base,{added:[f('blocked','é🙂')]});
    assert.equal(result.containsFact('allow',['é🙂']),false); result.close();
    result=inputs.apply(inputs.base,{removed:[f('blocked','é🙂')]});
    assert.equal(result.containsFact('allow',['é🙂']),true); result.close();
    inputs.close(); assert.equal(session.executionFingerprint,fingerprint);
  });
  test(`${runtime}: retained input stages both batches and recovers every rejection`, async t => {
    const {rules}=await basic(t), session=rules.prepare({capacities:{inputFacts:2}});
    const inputs=session.inputs({additionCapacity:3,removalCapacity:3,symbols:['known']});
    inputs.replace(inputs.base,[f('seed',1)]).close(); const base=inputs.base;
    for(const changes of [
      {added:[f('seed',2),f('seed',{})]}, {added:[f('seed',2)],removed:[f('seed',{})]},
      {added:[f('seed','unknown')]}, {added:[f('seed',2),f('seed',3)]},
      {added:[f('missing',1)]}, {removed:[f('missing','unknown')]},
      {added:Array(4).fill(f('seed',1))},
    ]) { assert.throws(()=>inputs.apply(base,changes)); assert.deepEqual(inputs.base,base); }
    const result=inputs.apply(base,{added:[f('seed',2)],removed:[f('seed',1)]});
    assert.equal(result.containsFact('allow',[1]),false); assert.equal(result.containsFact('allow',[2]),true);
    session.close(); inputs.close(); result.close(); assert.throws(()=>inputs.base,status(S.INVALID_STATE));
  });
  test(`${runtime}: retained input reentrancy, incarnation, zero bounds and bounded iteration`, async t => {
    const {rules,edb}=await basic(t), session=rules.prepare();
    assert.throws(()=>new api.SessionInputs(),TypeError);
    for(const v of [-1,1.5,true,Number.MAX_SAFE_INTEGER+1,1n<<64n]) assert.throws(()=>new api.InputBase(v,0));
    assert.equal(new api.InputBase((1n<<64n)-1n,0).incarnation,(1n<<64n)-1n);
    assert.throws(()=>session.inputs({get symbols(){session.close();}}),status(S.INVALID_STATE));
    const inputs=session.inputs({factCapacity:2}), base=inputs.base;
    assert.throws(()=>session.inputs(),status(S.INVALID_STATE));
    for(const action of [()=>session.close(),()=>inputs.close(),()=>inputs.apply(base),()=>rules.close()]) {
      function* facts(){yield f('seed',1); action();}
      assert.throws(()=>inputs.replace(base,facts()),status(S.INVALID_STATE)); assert.deepEqual(inputs.base,base);
    }
    assert.throws(()=>inputs.apply(base,{get removed(){inputs.close();}}),status(S.INVALID_STATE));
    let n=0; function* infinite(){while(true){++n;yield f('seed',1);}}
    assert.throws(()=>inputs.replace(base,infinite()),RangeError); assert.equal(n,3);
    inputs.close(); const other=session.inputs({factCapacity:0});
    assert.notEqual(other.base.incarnation,base.incarnation);
    assert.throws(()=>other.apply(base),status(S.INVALID_STATE));
    other.replace(other.base,[]).close(); assert.throws(()=>other.apply(other.base,{added:[f('seed',1)]}));
    session.close();
    const ordinary=rules.prepare(); ordinary.solve(rules.edb()).close();
    assert.throws(()=>ordinary.inputs(),status(S.INVALID_STATE));
  });
  test(`${runtime}: generated retained deltas versus complete snapshots (seed 170017)`, async t => {
    const {rules,edb}=await basic(t), session=rules.prepare(), oracle=rules.prepare();
    const inputs=session.inputs({factCapacity:32,additionCapacity:16,removalCapacity:16});
    let seed=170017, committed=new Map(); const next=()=>seed=(Math.imul(seed,1664525)+1013904223)>>>0;
    for(let turn=0;turn<80;++turn){
      const added=Array.from({length:7},()=>f((next()>>>16)%2?'seed':'blocked',next()%12));
      const removed=Array.from({length:7},()=>f((next()>>>16)%2?'seed':'blocked',next()%12));
      const key=x=>JSON.stringify(x); for(const x of removed)committed.delete(key(x)); for(const x of added)committed.set(key(x),x);
      const result=turn%9===0?inputs.replace(inputs.base,committed.values()):inputs.apply(inputs.base,{added,removed});
      edb.reset(); edb.addFacts(committed.values()); const reference=oracle.solve(edb);
      assert.deepEqual(result.enumeratePredicateFacts('allow',1),reference.enumeratePredicateFacts('allow',1));
      result.close();reference.close();
    }
  });
  test(`${runtime}: retained input solver diagnostics match the ordinary solve on rejection`, async t => {
    const {engine}=await basic(t);
    engine.registerDomain('js_tx_sum',[P.edb('ev',2),P.idbQuery('total',1)]);
    const rules=engine.loadInlineRuleset('js_tx_sum','main','total(N) :- sum(V,ev(_,V),N).');
    const session=rules.prepare(), inputs=session.inputs({factCapacity:4}), edb=rules.edb();
    inputs.replace(inputs.base,[f('ev',1,2147483647)]).close(); const base=inputs.base;
    for(const value of [1,-1]){
      edb.reset();edb.addFacts([f('ev',1,2147483647),f('ev',2,value)]);
      let expected; assert.throws(()=>rules.solve(edb),e=>{expected=e.diagnostic;return e.status===S.INVALID_FIELD;});
      assert.throws(()=>inputs.apply(base,{added:[f('ev',2,value)]}),e=>{
        assert.deepEqual(e.diagnostic,expected); return e.status===S.INVALID_FIELD;
      }); assert.deepEqual(inputs.base,base);
    }
    const result=inputs.apply(base,{added:[f('ev',2,1)],removed:[f('ev',1,2147483647)]});
    assert.equal(result.containsFact('total',[1]),true);
  });
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
  test(`${runtime}: empty manifest policy set contract`, async t => {
    const { engine } = await basic(t);
    const dir = await mkdtemp(resolve(tmpdir(), 'maelys-js-empty-manifest-'));
    t.after(() => rm(dir, { recursive: true, force: true }));
    for (const policies of [[], [{ policy_id: 'disabled', domain: 'js_common',
      file: 'missing.dl', sha256: '0'.repeat(64), mode: 'enforce', enabled: false,
      description: 'disabled' }]]) {
      const text = JSON.stringify({ policy_set_id: 'empty', policy_set_version: '1',
        manifest_version: '1', default_profile: 'enforce', created_for: 'test',
        strict_loading: true, fail_closed: true, capabilities: [], policies });
      const inputs = [{ text, policies: [] }];
      if (runtime === 'node') {
        const path = resolve(dir, 'manifest.json'); await writeFile(path, text);
        inputs.push({ path });
      }
      for (const input of inputs) {
        const rules = engine.loadManifest(input);
        assert.equal(rules.policyCount, 0);
        assert.equal(rules.fingerprint,
          '27a75b9d3186b42378465a7ced53f4da1465eb92a83a293594635213db667d31');
        assert.throws(() => rules.prepare({ policyIndex: 0 }));
        rules.close();
      }
    }
  });
  test(`${runtime}: in-memory manifests verify hashes, flags and policy selection`, async t => {
    const { engine } = await basic(t);
    const sources = ['allow(X) :- seed(X).', 'hidden(X) :- seed(X). allow(X) :- hidden(X), not(blocked(X)).'];
    const policies = sources.map((source, i) => ({ policy_id: `policy-${i}`, domain: 'js_common', file: `policy-${i}.dl`,
      sha256: createHash('sha256').update(source).digest('hex'), mode: 'enforce', enabled: true,
      description: 'JS shared contract', queries: [{ name: 'allow', arity: 1 }] }));
    const manifest = { policy_set_id: 'js.manifest', policy_set_version: '1', manifest_version: '1',
      default_profile: 'enforce', created_for: 'test', strict_loading: true, fail_closed: true, capabilities: [], policies };
    const bundle = sources.map((source, i) => ({ policyId: `policy-${i}`, source }));
    const load = () => engine.loadManifest({ text: JSON.stringify(manifest), policies: bundle });
    const rules = load(); assert.equal(rules.policyCount, 2);
    assert.deepEqual(rules.programCounts(0), {predicates:4,facts:0,rules:1});
    assert.deepEqual(rules.programCounts(1), {predicates:4,facts:0,rules:2});
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


  test(`${runtime}: profile limits, full input bounds and atomic overflow`, async t => {
    const {engine, rules, edb} = await basic(t), limits=engine.limits;
    assert.equal(limits.maxArity,4);
    assert.equal(limits.maxFactsPerPred,profile==='large'?256:64);
    assert.equal(limits.maxEdbFacts,profile==='large'?2048:1024);
    assert.equal(limits.maxStringBytes,1024);
    assert.ok(limits.inputEdbTextBytes>=limits.stringPoolBytes);
    assert.equal(Object.keys(limits).length,14);
    assert.equal(limits.maxPolicyAtoms,256);
    assert.equal(limits.maxPolicyAtomBytes,63);
    edb.addFacts(Array.from({length:limits.maxEdbFacts},()=>f('seed','same')));
    const full=edb.usage;
    assert.throws(()=>edb.addFact('seed',['one-more']),RangeError);
    assert.deepEqual(edb.usage,full);
    assert.deepEqual(rules.solve(edb).enumeratePredicateFacts('allow',1),[['same']]);
    edb.reset();
    const size=Math.min(limits.maxStringBytes,1000);
    const texts=Array.from({length:Math.ceil(limits.inputEdbTextBytes/size)+1},(_,i)=>f('seed',`${i}:`+'x'.repeat(size-8)));
    assert.throws(()=>edb.addFacts(texts),status(S.PAYLOAD_TOO_LARGE));
    assert.equal(edb.count,0); assert.equal(edb.usage.textBytes,0);
  });
  test(`${runtime}: copied declarations, explicit policy atoms and rejected-load recovery`, async t => {
    const engine=await Engine.create({profile}); t.after(()=>engine.close());
    const predicates=[{name:'base',arity:1,flags:12},{name:'q',arity:1,flags:6}], atoms=['blue'];
    engine.registerDomain('js_atoms',predicates,atoms);
    engine.registerDomain('js_atoms',predicates,atoms);
    atoms[0]='red'; predicates[0].name='changed';
    assert.throws(()=>engine.registerDomain('js_atoms',predicates,atoms),status(S.INVALID_FIELD));
    assert.throws(()=>engine.loadInlineRuleset('js_atoms','bad','base("red"). q(X) :- base(X).'));
    const rules=engine.loadInlineRuleset('js_atoms','good','base("blue"). q(X) :- base(X).');
    assert.deepEqual(rules.programCounts(), {predicates:2,facts:1,rules:1});
    const result=rules.solve(rules.edb());
    assert.equal(result.containsFact('base',['blue']),true);
    assert.deepEqual(result.enumeratePredicateFacts('base',1),[]);
    assert.match(result.explainTrue('base',['blue']),/status=not-derived/);
    assert.equal(result.containsFact('q',['blue']),true);
    engine.registerDomain('js_stack',[P.edb('e',1),P.idbQuery('q',1)]);
    const padding='%'+ 'x'.repeat(100000)+'\n';
    for(let i=0;i<3;++i) {
      assert.throws(()=>engine.loadInlineRuleset('js_stack','bad',padding+'q(X) :- e(Y).'),e=>e.diagnostic.message.includes('head variable'));
      const good=engine.loadInlineRuleset('js_stack','ok',padding+'q(X) :- e(X).');
      assert.equal(good.solve(good.edb()).derivedFactCount(),0); good.close();
    }
  });
  test(`${runtime}: normalized program counts are distinct from session quotas`, async t => {
    const {engine} = await basic(t);
    engine.registerDomain('js_counts',[P.edb('seed',1),P.policyFact('fixed',1),P.idbQuery('out',1),P.edb('unused',1)]);
    const rules=engine.loadInlineRuleset('js_counts','counts','fixed(3). out(X) :- seed(X) or fixed(X).');
    const counts=rules.programCounts(), fingerprint=rules.fingerprint;
    assert.deepEqual(counts,{predicates:4,facts:1,rules:2});
    assert.ok(Object.isFrozen(counts));
    assert.equal(rules.prepare({capacities:{inputFacts:0}}).capacities.inputFacts,0);
    assert.deepEqual(rules.programCounts(),counts);
    assert.equal(rules.fingerprint,fingerprint);
    assert.throws(()=>rules.programCounts(1),status(S.NOT_FOUND));
    for(const index of [-1,true,0.5,2**32]) assert.throws(()=>rules.programCounts(index),RangeError);
    rules.close(); assert.throws(()=>rules.programCounts(),status(S.INVALID_STATE));
  });
  test(`${runtime}: bounded solve diagnostics and successful retry`, async t => {
    const engine=await Engine.create({profile}); t.after(()=>engine.close());
    const cap=engine.limits.maxFactsPerPred;
    engine.registerDomain('js_overflow',[P.edb('a',1),P.edb('b',1),P.idbQuery('sg',2)]);
    const rules=engine.loadInlineRuleset('js_overflow','main','sg(X,Y) :- a(X),b(Y).'), edb=rules.edb();
    const n=Math.floor(Math.sqrt(cap))+1;
    edb.addFacts([...Array.from({length:n},(_,i)=>f('a',i)),...Array.from({length:n},(_,i)=>f('b',i))]);
    let diagnostic;
    assert.throws(()=>rules.solve(edb),e=>{
      diagnostic=e.diagnostic; return e.status===S.PAYLOAD_TOO_LARGE && diagnostic.codeName==='solve_idb_overflow';
    });
    assert.equal(diagnostic.present & 6n,6n);
    assert.deepEqual(diagnostic.predicate,{name:'sg',arity:2});
    assert.deepEqual(diagnostic.capacity,{observed:cap+1,limit:cap,kind:10});
    assert.equal(edb.count,n*2); edb.clear(); edb.addFacts([f('a',1),f('b',2)]);
    assert.equal(rules.solve(edb).containsFact('sg',[1,2]),true);
    assert.equal(diagnostic.capacity.observed,cap+1);
  });
  test(`${runtime}: aggregate operand diagnostics preserve exact int64`, async t => {
    const engine=await Engine.create({profile}); t.after(()=>engine.close());
    engine.registerDomain('js_operand',[P.edb('e',2),P.idbQuery('q',1)]);
    for(const op of ['min','max','sum']) {
      const rules=engine.loadInlineRuleset('js_operand',op,`q(N) :- ${op}(V,e(_,V),N).`), edb=rules.edb();
      for(const value of [-1n,-(1n<<63n),(1n<<63n)-1n,2147483648n,true,'wrong']) {
        edb.reset(); edb.addFact('e',[1,value]);
        assert.throws(()=>rules.solve(edb),error=>{
          const d=error.diagnostic;
          assert.equal(error.status,S.INVALID_FIELD); assert.equal(d.codeName,'solve_aggregate_domain_error');
          assert.deepEqual(d.predicate,{name:'e',arity:2});
          assert.equal(d.aggregate.operator,op); assert.equal(d.aggregate.value,String(value));
          assert.equal(d.aggregate.termIndex,1); assert.equal(d.aggregate.limit,2147483647);
          assert.equal(d.capacity,undefined); assert.ok(Object.isFrozen(d.aggregate)); return true;
        });
        edb.clear(); edb.addFact('e',[1,7]); const result=rules.solve(edb);
        assert.equal(result.containsFact('q',[7]),true); result.close();
      }
      rules.close();
    }
  });
  if(runtime==='wasm') test('wasm: exact exports, balanced transport allocations and memory growth', async t => {
    const factory=(await import(pathToFileURL(resolve(packageRoot,`wasm/${profile}/engine.mjs`)))).default;
    const mod=await factory();
    const engine=await Engine.create({profile,wasmFactory:async()=>mod}); t.after(()=>engine.close());
    const bytes=await readFile(resolve(packageRoot,`wasm/${profile}/engine.wasm`));
    const names=WebAssembly.Module.exports(await WebAssembly.compile(bytes)).map(x=>x.name);
    assert.deepEqual(names.filter(n=>n.startsWith('maelys_')).sort(),
      ['create','destroy','call','words','word_count','text','scalar','diagnostic'].map(n=>'maelys_js_'+n).sort());
    const malloc=mod._malloc, free=mod._free; let live=0;
    mod._malloc=size=>{const ptr=malloc(size); if(ptr) ++live; return ptr;};
    mod._free=ptr=>{if(ptr) --live; free(ptr);};
    t.after(()=>{mod._malloc=malloc;mod._free=free;});
    engine.registerDomain('js_memory',[P.edb('e',1),P.idbQuery('q',1)]);
    const rules=engine.loadInlineRuleset('js_memory','main','q(X) :- e(X).'), edb=rules.edb(); edb.addFact('e',['x']);
    const result=rules.solve(edb), expected=result.explainTrue('q',['x']);
    for(let i=0;i<20;++i) assert.equal(result.explainTrue('q',['x']),expected);
    assert.equal(live,0);
    mod._malloc=()=>0;
    assert.throws(()=>result.explainTrue('q',['x']),/allocation failed/);
    mod._malloc=malloc; mod._free=free;
    const before=mod.HEAPU8.buffer.byteLength, big=mod._malloc(before+65536);
    assert.ok(big); assert.ok(mod.HEAPU8.buffer.byteLength>before); mod._free(big);
    assert.deepEqual(result.enumeratePredicateFacts('q',1),[['x']]);
  });

}
