import { MaelysPlayground, PredKind, Status, DatalogError, type Value } from '../../bindings/wasm/maelys_playground';
async function consume(factory: Parameters<typeof MaelysPlayground.create>[0]) {
  const pg = await MaelysPlayground.create(factory, 'engine.wasm');
  pg.registerDomain({name: 'd', predicates: [{name: 'p', arity: 4, flags: PredKind.EDB | PredKind.QUERY}], atoms: ['a']});
  pg.loadPolicy('d', 'p', 'source').addFacts([{predicate: 'p', terms: ['a', 1n, true, 2]}]).solve();
  const rows: Value[][] = pg.enumerate('p', 4);
  const present: boolean = pg.query('p', rows[0]);
  const texts: string[] = [pg.explainTrue('p', rows[0]), pg.explainFalse('p', rows[0]), pg.fingerprints().execution];
  const counts: number[] = [pg.buildLimits().maxEdbFacts, pg.inputUsage().textCapacity, pg.derivedFactCount()];
  pg.freeResult().clearFacts();
  // @ts-expect-error result integers are never numbers
  const lost: number = rows[0][0];
  // @ts-expect-error no input symbol IDs
  pg.addFacts([{predicate: 'p', terms: [{symbolId: 1}]}]);
  // @ts-expect-error historical method removed
  pg.querySymbol('p', 'a');
  pg.close();
  return {present, texts, counts, lost};
}
function diagnostic(error: DatalogError) {
  const mask: bigint = error.diagnostic.present;
  const status: number = error.status || Status.INVALID_ARGUMENT;
  // @ts-expect-error snapshots are immutable
  error.diagnostic.capacity!.limit = 2;
  return {mask, status};
}
void consume; void diagnostic;
