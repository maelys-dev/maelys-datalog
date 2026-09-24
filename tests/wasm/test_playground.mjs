/* SPDX-License-Identifier: MPL-2.0 */
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { create, fact as f, pred, P, S, status, MaelysPlayground } from './helpers.mjs';

async function basic() {
  const pg = await create();
  pg.registerDomain({ name: 'test', predicates: [pred('e', 1, P.EDB | P.QUERY), pred('q', 1, P.IDB | P.QUERY)] });
  pg.loadPolicy('test', 'main', 'q(X) :- e(X).');
  return pg;
}

test('typed int64 transport, bool/symbol identity, copies, independent instances', async () => {
  const pg = await basic(), other = await basic();
  const values = [-(1n << 63n), (1n << 63n) - 1n, 9007199254740993n, 0n, 1, true, false, '1', '', 'é🙂'];
  const batch = values.map(x => f('e', x));
  pg.addFacts(batch); batch[0].terms[0] = 'mutated';
  pg.solve();
  for (const value of values) assert.equal(pg.query('q', [value]), true);
  assert.equal(pg.derivedFactCount(), values.length);
  const copied = pg.enumerate('q', 1).flat();
  assert.equal(copied.filter(x => typeof x === 'bigint').length, 5);
  assert.deepEqual(new Set(copied), new Set(values.map(x => typeof x === 'number' ? BigInt(x) : x)));
  assert.deepEqual(other.inputUsage().facts, 0);
  other.solve(); assert.equal(other.query('q', ['1']), false);
  pg.freeResult().clearFacts().addFacts([f('e', 'next')]).solve();
  assert.equal(pg.query('q', ['1']), false);
  pg.close(); other.close();
  assert.ok(copied.includes('é🙂'));
});

test('arities zero through four and resolved output types', async () => {
  const pg = await create();
  pg.registerDomain({ name: 'arity', predicates: Array.from({ length: 5 }, (_, n) => [
    pred(`e${n}`, n, P.EDB), pred(`q${n}`, n, P.IDB | P.QUERY)]).flat() });
  pg.loadPolicy('arity', 'main', Array.from({ length: 5 }, (_, n) => {
    const vars = ['A','B','C','D'].slice(0,n).join(','); return `q${n}(${vars}) :- e${n}(${vars}).`;
  }).join('\n'));
  const values = ['🙂', 9223372036854775807n, true, ''];
  pg.addFacts(Array.from({ length: 5 }, (_, n) => f(`e${n}`, ...values.slice(0,n)))).solve();
  for (let n = 0; n < 5; ++n) {
    assert.equal(pg.query(`q${n}`, values.slice(0,n)), true);
    assert.deepEqual(pg.enumerate(`q${n}`, n), [values.slice(0,n)]);
  }
  pg.close();
});

test('invalid JS values and malformed UTF-16 never publish a partial batch', async () => {
  const pg = await basic(); pg.addFacts([f('e', 'keep')]);
  const before = pg.inputUsage();
  for (const bad of [NaN, Infinity, 1.5, 9007199254740992, 1n << 63n, -(1n << 63n) - 1n,
    null, undefined, {}, 'bad\0tail', '\ud800', '\udc00']) {
    assert.throws(() => pg.addFacts([f('e', 'new'), f('e', bad)]));
    assert.deepEqual(pg.inputUsage(), before);
  }
  assert.throws(() => pg.addFacts([f('e', 1,2,3,4,5)]));
  pg.addFacts([]).solve(); assert.deepEqual(pg.enumerate('q', 1), [['keep']]); pg.close();
});

test('heterogeneous batch atomicity, copied text, text and raw-entry capacities', async () => {
  const pg = await basic(), limits = pg.buildLimits();
  pg.addFacts([f('e', 'keep')]); const before = pg.inputUsage();
  assert.throws(() => pg.addFacts([f('e', 'new'), f('different', 'x'.repeat(limits.maxStringBytes + 1))]), status(S.PAYLOAD_TOO_LARGE));
  assert.deepEqual(pg.inputUsage(), before);
  pg.clearFacts().addFacts(Array.from({ length: limits.maxEdbFacts }, () => f('e', 'same')));
  assert.equal(pg.inputUsage().facts, limits.maxEdbFacts);
  const full = pg.inputUsage();
  assert.throws(() => pg.addFacts([f('e', 'one-more')]), status(S.PAYLOAD_TOO_LARGE));
  assert.deepEqual(pg.inputUsage(), full);
  pg.solve(); assert.deepEqual(pg.enumerate('q', 1), [['same']]);
  pg.freeResult().clearFacts();
  const size = Math.min(limits.maxStringBytes, 1000);
  const texts = Array.from({ length: Math.ceil(limits.inputEdbTextBytes / size) + 1 }, (_, i) => f('e', `${i}:` + 'x'.repeat(size - 8)));
  assert.throws(() => pg.addFacts(texts), status(S.PAYLOAD_TOO_LARGE));
  assert.equal(pg.inputUsage().facts, 0); assert.equal(pg.inputUsage().textBytes, 0);
  pg.close();
});

test('domain atoms explicit, no permissive inline load, register copies and conflicts', async () => {
  const pg = await create();
  const domain = { name: 'atoms', predicates: [pred('base', 1, P.POLICY_FACT | P.QUERY), pred('q', 1, P.IDB | P.QUERY)], atoms: ['blue'] };
  pg.registerDomain(domain).registerDomain(domain);
  domain.atoms[0] = 'red'; domain.predicates[0].name = 'changed';
  assert.throws(() => pg.registerDomain(domain), status(S.INVALID_FIELD));
  assert.throws(() => pg.loadPolicy('atoms', 'bad', 'base("red"). q(X) :- base(X).'));
  pg.loadPolicy('atoms', 'good', 'base("blue"). q(X) :- base(X).').solve();
  assert.equal(pg.query('base', ['blue']), true);
  assert.deepEqual(pg.enumerate('base', 1), []);
  assert.match(pg.explainTrue('base', ['blue']), /status=not-derived/);
  assert.equal(pg.query('q', ['blue']), true); pg.close();
});

test('all four aggregates receive typed EDB; full-fact sum and empty groups', async () => {
  const pg = await create();
  pg.registerDomain({ name: 'aggregates', predicates: [pred('group',1,P.EDB), pred('event',3,P.EDB),
    ...['n','lo','hi','total'].map(x => pred(x,2,P.IDB|P.QUERY))] });
  pg.loadPolicy('aggregates','main','n(G,N) :- group(G),count(V,event(_,G,V),N).\n' +
    'lo(G,N) :- group(G),min(V,event(_,G,V),N).\nhi(G,N) :- group(G),max(V,event(_,G,V),N).\n' +
    'total(G,N) :- group(G),sum(V,event(_,G,V),N).');
  pg.addFacts([f('group','api'),f('group','empty'),f('event',1,'api',5),f('event',2,'api',5),f('event',3,'api',9),f('event',2,'api',5)]).solve();
  for (const [name,value] of [['n',2n],['lo',5n],['hi',9n],['total',19n]]) {
    assert.equal(pg.query(name,['api',value]),true);
    assert.match(pg.explainTrue(name,['api',value]), /kind=(count|min|max|sum)/);
  }
  assert.equal(pg.query('total',['empty',0]),true); assert.equal(pg.query('n',['empty',0]),true);
  assert.equal(pg.enumerate('lo',2).length,1); assert.equal(pg.enumerate('hi',2).length,1);
  pg.freeResult().clearFacts().addFacts([f('group','api'),f('event',1,'api',2147483647),f('event',2,'api',1)]);
  assert.throws(() => pg.solve(),status(S.INVALID_FIELD));
  assert.equal(pg.inputUsage().facts,3); assert.throws(() => pg.derivedFactCount(),status(S.INVALID_STATE));
  pg.clearFacts().addFacts([f('group','api')]).solve(); assert.equal(pg.query('total',['api',0]),true); pg.close();
});

test('count distinguishes integer boolean symbol, deduplicates full repeated fact', async () => {
  const pg = await create();
  pg.registerDomain({ name:'kinds', predicates:[pred('e',1,P.EDB),pred('n',1,P.IDB|P.QUERY)] });
  pg.loadPolicy('kinds','main','n(N) :- count(X,e(X),N).');
  pg.addFacts([f('e',1),f('e',true),f('e','1'),f('e',1)]).solve();
  assert.deepEqual(pg.enumerate('n',1),[[3n]]); pg.close();
});

test('Why-false, unknown symbols, permissions and diagnostic snapshots', async () => {
  const pg = await create();
  pg.registerDomain({name:'explain',predicates:[pred('e',1,P.EDB|P.QUERY),pred('blocked',1,P.EDB),pred('q',1,P.IDB|P.QUERY)]});
  pg.loadPolicy('explain','main','q(X) :- e(X),not(blocked(X)).');
  pg.addFacts([f('e','alice'),f('e','bob'),f('blocked','bob')]).solve();
  assert.equal(pg.query('q',['alice']),true); assert.equal(pg.query('q',['bob']),false);
  assert.match(pg.explainTrue('q',['alice']),/MAELYS-DATALOG-v2/);
  assert.match(pg.explainTrue('q',['bob']),/status=not-derived/);
  const why = pg.explainFalse('q',['bob']); assert.match(why,/document=why-false/); assert.match(why,/status=complete/);
  assert.match(pg.explainFalse('q',['alice']),/status=not-applicable/);
  assert.equal(pg.query('q',['never-interned']),false);
  for (const method of ['explainTrue','explainFalse']) assert.throws(() => pg[method]('q',['never-interned']),status(S.NOT_FOUND));
  assert.throws(() => pg.query('blocked',['never-interned']),status(S.INVALID_FIELD));
  assert.throws(() => pg.query('q',[])); assert.throws(() => pg.enumerate('blocked',1),status(S.INVALID_FIELD));
  assert.throws(() => pg.query('missing',['never-interned']));
  assert.equal(pg.inputUsage().facts,3); assert.equal(pg.explainFalse('q',['bob']),why);
  pg.close();
});

test('lifecycle: live lease, failed solve retains input, failed load reusable, close', async () => {
  const pg = await basic();
  assert.throws(() => new MaelysPlayground(pg._mod),/one wrapper/);
  assert.throws(() => pg.query('q',['x']),status(S.INVALID_STATE));
  pg.addFacts([f('missing','x')]); assert.throws(() => pg.solve()); assert.equal(pg.inputUsage().facts,1);
  pg.clearFacts().addFacts([f('e','x')]).solve();
  for (const call of [() => pg.solve(),() => pg.addFacts([]),() => pg.clearFacts(),() => pg.loadPolicy('test','other','q(X) :- e(X).')])
    assert.throws(call,status(S.INVALID_STATE));
  assert.equal(pg.query('q',['x']),true);
  pg.freeResult();
  let saved;
  try { pg.loadPolicy('test','bad','q(X) :- e(Y).'); } catch(e) { saved=e.diagnostic; }
  assert.ok(saved?.message.includes('head variable'));
  pg.solve(); assert.equal(pg.query('q',['x']),true);
  assert.ok(saved.message.includes('head variable')); assert.ok(Object.isFrozen(saved));
  pg.close();
  for (const call of [() => pg.close(),() => pg.freeResult(),() => pg.buildLimits(),() => pg.inputUsage(),() => pg.solve(),() => pg.query('q',['x'])])
    assert.throws(call,status(S.INVALID_STATE));
});

test('same-generation capacity reports actual predicate and attempted count in both profiles', async () => {
  const pg = await create(), cap=pg.buildLimits().maxFactsPerPred;
  pg.registerDomain({name:'overflow',predicates:[pred('a',1,P.EDB),pred('b',1,P.EDB),pred('sg',2,P.IDB|P.QUERY)]});
  pg.loadPolicy('overflow','main','sg(X,Y) :- a(X),b(Y).');
  const n=Math.floor(Math.sqrt(cap))+1;
  pg.addFacts([...Array.from({length:n},(_,i)=>f('a',i)),...Array.from({length:n},(_,i)=>f('b',i))]);
  let diagnostic;
  assert.throws(() => pg.solve(),e => {
    diagnostic=e.diagnostic;
    return e.status===S.PAYLOAD_TOO_LARGE && diagnostic.codeName==='solve_idb_overflow';
  });
  assert.equal(diagnostic.present & 6n,6n);
  assert.equal(diagnostic.predicate.name,'sg');
  assert.deepEqual(diagnostic.capacity,{observed:cap+1,limit:cap,kind:10});
  assert.equal(pg.inputUsage().facts,n*2);
  pg.clearFacts().addFacts([f('a',1),f('b',2)]).solve();
  assert.equal(pg.query('sg',[1,2]),true); assert.equal(diagnostic.capacity.observed,cap+1); pg.close();
});

test('fingerprints do not depend on input; transport allocations balanced, growth views refreshed', async () => {
  const pg=await basic(), initial=pg.fingerprints();
  for(const value of Object.values(initial)) assert.match(value,/^[0-9a-f]{64}$/);
  const mod=pg._mod, malloc=mod._malloc, free=mod._free; let live=0;
  mod._malloc=size=>{ const ptr=malloc(size); if(ptr) ++live; return ptr; };
  mod._free=ptr=>{ if(ptr) --live; free(ptr); };
  pg.addFacts([f('e','x')]).solve();
  assert.deepEqual(pg.fingerprints(),initial);
  const expected=pg.explainTrue('q',['x']);
  for(let i=0;i<20;++i) assert.equal(pg.explainTrue('q',['x']),expected);
  assert.equal(live,0);
  mod._malloc=()=>0;
  assert.throws(()=>pg.explainTrue('q',['x']),/allocation failed/);
  mod._malloc=malloc; mod._free=free;
  assert.equal(pg.query('q',['x']),true);
  const before=mod.HEAPU8.buffer.byteLength;
  const big=mod._malloc(before+65536); assert.ok(big); assert.ok(mod.HEAPU8.buffer.byteLength>before); mod._free(big);
  assert.deepEqual(pg.enumerate('q',1),[['x']]); pg.close();
});

test('OR expansion preserves canonical text, bounded Why-true and Why-false stay visible', async () => {
  const build=async source=>{
    const pg=await create();
    pg.registerDomain({name:'or_example',predicates:[pred('edge',2,P.EDB),pred('missing',2,P.EDB),pred('path',2,P.IDB|P.QUERY)]});
    pg.loadPolicy('or_example','main',source);
    pg.addFacts([f('edge','a','b')]).solve(); return pg;
  };
  const a=await build('path(X,Y) :- edge(X,Y) or missing(X,Y).');
  const b=await build('path(X,Y) :- edge(X,Y). path(X,Y) :- missing(X,Y).');
  assert.equal(a.explainTrue('path',['a','b']),b.explainTrue('path',['a','b']));
  a.close(); b.close();
  const pg=await create();
  pg.registerDomain({name:'depth',predicates:[pred('edge',2,P.EDB),pred('path',2,P.IDB|P.QUERY),pred('reach',2,P.IDB|P.QUERY)]});
  pg.loadPolicy('depth','main','path(X,Y) :- edge(X,Y). path(X,Z) :- path(X,Y),edge(Y,Z). reach(X,Y) :- path(X,Y).');
  pg.addFacts(Array.from({length:8},(_,i)=>f('edge',`n${i}`,`n${i+1}`))).solve();
  assert.equal(pg.query('path',['n0','n8']),true);
  assert.match(pg.explainTrue('path',['n0','n8']),/status=truncated/);
  pg.close();
  const absent=await create();
  absent.registerDomain({name:'false_limit',predicates:[pred('edge',2,P.EDB),pred('never',1,P.EDB),pred('q',1,P.IDB|P.QUERY)]});
  absent.loadPolicy('false_limit','main','q(X) :- edge(X,Y),never(Y).');
  absent.addFacts(Array.from({length:40},(_,i)=>f('edge','x',i))).solve();
  assert.equal(absent.query('q',['x']),false);
  assert.match(absent.explainFalse('q',['x']),/status=truncated/);
  absent.close();
});
