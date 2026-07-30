import playgroundPkg from '../../js/maelys_playground.js';

const { MaelysPlayground, PredKind } = playgroundPkg;

const profile = (process.env.MAELYS_WASM_PROFILE || 'small').toLowerCase();
if (profile !== 'small' && profile !== 'large') {
  throw new Error(`Unsupported MAELYS_WASM_PROFILE="${profile}"`);
}

const buildDir = profile === 'large' ? '../../build/wasm-large' : '../../build/wasm';
const dynamicModule = await import(
  new URL(`${buildDir}/maelys_datalog_dynamic.js`, import.meta.url).href
);
const MaelysDatalogDynamic = dynamicModule.default;
const wasmUrl = new URL(`${buildDir}/maelys_datalog_dynamic.wasm`, import.meta.url).href;
const mod = await MaelysDatalogDynamic({
  locateFile: (file) => (file.endsWith('.wasm') ? wasmUrl : file),
});
const pg = new MaelysPlayground(mod);

const memoryBefore = mod.HEAP32.buffer.byteLength;

pg.domainBegin('stack_safety');
pg.domainAddPredicate('safe', 1, PredKind.EDB);
pg.domainAddPredicate('allow', 1, PredKind.IDB | PredKind.QUERY);
pg.domainCommit();

let threw = false;
try {
  pg.loadRuleset('stack_safety', 'stack-safety.main', 'allow(X) :- safe(Y).\n');
} catch {
  threw = true;
}

if (!threw) {
  throw new Error('unsafe rule must be rejected');
}

const expectedMessage = 'head variable not bound by positive body atom';
const expectedHint = 'bind every head variable in a positive body atom';
if (pg.diagMessage !== expectedMessage) {
  throw new Error(`expected message "${expectedMessage}", got "${pg.diagMessage}"`);
}
if (pg.diagHint !== expectedHint) {
  throw new Error(`expected hint "${expectedHint}", got "${pg.diagHint}"`);
}

const memoryAfter = mod.HEAP32.buffer.byteLength;
if (memoryAfter !== memoryBefore) {
  throw new Error(
    `linear memory grew during rejected load: before=${memoryBefore}, after=${memoryAfter}`,
  );
}

console.log(
  `PASS: manifest stack safety ${profile} ` +
  `memory_before=${memoryBefore} memory_after=${memoryAfter}`,
);
