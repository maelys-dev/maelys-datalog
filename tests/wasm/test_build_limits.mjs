import playgroundPkg from '../../js/maelys_playground.js';

const { MaelysPlayground } = playgroundPkg;

const profile = (process.env.MAELYS_WASM_PROFILE || 'small').toLowerCase();
const buildDir = profile === 'large' ? '../../build/wasm-large' : '../../build/wasm';

const dynamicModule = await import(new URL(`${buildDir}/maelys_datalog_dynamic.js`, import.meta.url).href);
const MaelysDatalogDynamic = dynamicModule.default;

const expectedByProfile = {
  small: {
    maxSymbols: 512,
    stringPoolBytes: 32768,
    maxPredicates: 128,
    maxRules: 128,
    maxArity: 4,
    maxBodyLiterals: 8,
    maxDepth: 10,
    maxEdbFacts: 1024,
    maxIdbFacts: 1024,
    maxFactsPerPred: 64,
  },
  large: {
    maxSymbols: 512,
    stringPoolBytes: 32768,
    maxPredicates: 128,
    maxRules: 128,
    maxArity: 4,
    maxBodyLiterals: 8,
    maxDepth: 10,
    maxEdbFacts: 2048,
    maxIdbFacts: 2048,
    maxFactsPerPred: 256,
  },
};

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

if (!Object.prototype.hasOwnProperty.call(expectedByProfile, profile)) {
  throw new Error(`Unsupported MAELYS_WASM_PROFILE="${profile}"`);
}

const wasmUrl = new URL(`${buildDir}/maelys_datalog_dynamic.wasm`, import.meta.url).href;
const pg = await MaelysPlayground.create(MaelysDatalogDynamic, wasmUrl);

assert(typeof pg.buildLimits === 'function', 'buildLimits must be a function');
const limits = pg.buildLimits();
assert(limits && typeof limits === 'object', 'buildLimits must return an object');

const expected = expectedByProfile[profile];
const expectedKeys = Object.keys(expected);
const actualKeys = Object.keys(limits);
assert(actualKeys.length === expectedKeys.length,
       `expected ${expectedKeys.length} keys, got ${actualKeys.length}: ${actualKeys.join(', ')}`);

for (const key of expectedKeys) {
  assert(Object.prototype.hasOwnProperty.call(limits, key), `missing ${key}`);
  assert(Number.isInteger(limits[key]), `${key} must be an integer, got ${limits[key]}`);
  assert(limits[key] >= 0, `${key} must be non-negative, got ${limits[key]}`);
  assert(limits[key] === expected[key],
         `${key}: expected ${expected[key]} for ${profile}, got ${limits[key]}`);
}

console.log(`PASS: buildLimits ${profile} ${JSON.stringify(limits)}`);
