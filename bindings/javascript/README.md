# @maelys-dev/datalog

One JavaScript/TypeScript consumer API, with two implementations:

- `@maelys-dev/datalog/node`: a native Node-API addon with the engine compiled in.
- `@maelys-dev/datalog/wasm`: WebAssembly for browsers, module workers, or Node.

`engine.limits` queries the loaded SDK, including `maxPolicyAtoms` and
`maxPolicyAtomBytes` (UTF-8 bytes excluding NUL). `ruleset.programCounts(index=0)`
reads immutable compiled counts without preparing a session: `predicates`
includes unused registry declarations, `facts` counts policy facts, and `rules`
counts normalized rules, including expanded OR alternatives. These are distinct
from `session.capacities` (effective quotas) and EDB/result occupancy. This source
binding requires the consumer-introspection feature guard from its matching SDK;
mixing it with a transport lacking those queries fails explicitly.

The root import selects native Node in Node and WASM with the `browser` export
condition. Browser code without a bundler uses the ESM WASM entry point. No
runtime silently falls back to another backend. The native artifact targets
Linux x64/arm64 with glibc **2.34 or newer**, and macOS arm64 **13.5 or newer**,
with Node-API 8. The configured CI matrix covers Node 22, 24 and 26; Node's own
OS requirements also apply. Both runtimes expose SMALL and LARGE engine profiles.
Windows, musl/Alpine and macOS x64 have no native prebuild. Select `/wasm`
explicitly there. An unsupported platform or failed native load reports this
choice without silently changing runtimes.

Packages are released through GitHub Packages. Configure the scope and an
appropriate package-read token in your npm configuration, then install the package:

```text
@maelys-dev:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=${GITHUB_TOKEN}
```

```sh
npm install @maelys-dev/datalog
```

```ts
import { Engine, Predicate } from '@maelys-dev/datalog/wasm';

const engine = await Engine.create({ profile: 'small' });
try {
  engine.registerDomain('access', [
    Predicate.edb('member', 2),
    Predicate.edb('document', 2),
    Predicate.edb('blocked', 1),
    Predicate.idbQuery('can_read', 2),
  ]);
  const rules = engine.loadInlineRuleset('access', 'access.v1',
    'can_read(U,D) :- member(U,G), document(D,G), not(blocked(U)).');
  const input = rules.edb();
  input.addFacts([
    { predicate: 'member', terms: ['alice', 'engineering'] },
    { predicate: 'member', terms: ['bob', 'engineering'] },
    { predicate: 'member', terms: ['carol', 'finance'] },
    { predicate: 'document', terms: ['design', 'engineering'] },
    { predicate: 'document', terms: ['roadmap', 'engineering'] },
    { predicate: 'document', terms: ['budget', 'finance'] },
    { predicate: 'blocked', terms: ['bob'] },
  ]);
  const session = rules.prepare({ explanations: 3 });
  const result = session.solve(input);
  console.log(result.containsFact('can_read', ['alice', 'design'])); // true
  console.log(result.explainFalse('can_read', ['bob', 'design']));
  result.close(); // releases the session's result lease
} finally {
  engine.close(); // also closes all remaining descendants
}
```

Switching the import to `/node` retains the same API and result values. Public
classes mirror the Python consumer's capabilities: independent rulesets, input
buffers, prepared sessions, result leases, explicit capacities, typed values,
manifest permissions, fingerprints, structured errors and both explanation kinds.
JavaScript names use camelCase. Integers returned by the engine are always
`bigint`; input `number` values must be safe integers. Capacity/transport lengths
use uint32 in both implementations so their accepted range is portable to wasm32.

An EDB belongs to one ruleset. A successful solve freezes append and clear until
`reset()`. Reset does not change any existing result snapshot. A session permits
one live result; `result.close()` permits another solve. `rules.solve(input)`
creates a private session that closes with its result. Closing a ruleset or engine
cascades. Close is idempotent; classes also implement `Symbol.dispose`.
`enumeratePredicateFacts` returns copies, while `enumerateRaw` returns terms bound
to their result. A closed or foreign result cannot resolve them.

All batch values are staged before one native append. Conversion, text-capacity
and fact-capacity failures preserve the prior EDB. Conversion buffers, JavaScript
objects, returned strings and explanation text still allocate; the complete
binding is not allocation-free. Explanation workspace is opt-in.

## Retained input transactions (0.17.0)

Node and WASM expose the same `SessionInputs` and immutable `InputBase`:

```ts
// Given seed/1 EDB and allow/1 query IDB, allow(X) :- seed(X).
const session = rules.prepare({ explanations: 3 });
const inputs = session.inputs({ factCapacity: 64, additionCapacity: 8,
  removalCapacity: 8, symbols: ['alice', 'bob'] });
try {
  const first = inputs.replace(inputs.base, [{ predicate: 'seed', terms: ['alice'] }]);
  try { console.log(first.containsFact('allow', ['alice'])); }
  finally { first.close(); }
  const next = inputs.apply(inputs.base, {
    added: [{ predicate: 'seed', terms: ['bob'] }],
    removed: [{ predicate: 'seed', terms: ['alice'] }],
  });
  try { console.log(next.containsFact('allow', ['bob'])); }
  finally { next.close(); }
} finally { session.close(); }
```

Attach before the first successful solve. While attached, ordinary `solve(edb)`
is unavailable. `replace(base, facts)` supplies the complete dynamic input;
`apply(base, {added, removed})` applies a delta to the committed set. Added facts
win over removals, duplicates collapse and absent removals are no-ops. Each call
solves and commits immediately; every success, even a no-op, returns a result
lease and advances the generation. Close that result before reuse or detachment.
Closing the session closes the result first, then its attachment.

`inputs.base` returns an `InputBase` with `bigint` incarnation and generation.
Its constructor accepts exact unsigned 64-bit integers; never coerce them through
an inexact `number`. A stale or foreign base fails with `INVALID_STATE`. Pass the
base used to compute the batch; there is no hidden base refresh or retry.

`factCapacity` defaults to E; `additionCapacity` and `removalCapacity` default to
it. Zero is an exact bound. The raw replacement and add/remove bounds apply before
deduplication; the committed set must fit its own bound and the session quotas.
Explicit add/remove bounds may exceed E up to the loaded build's input ceiling.
Program symbols plus `symbols` form a fixed vocabulary, including empty strings.
Unknown additions/replacements fail; unknown removed symbols denote absent facts
after validation. Both batches are fully staged before C is called. Bad getters,
iterators, types, capacity, domain and solve failures publish no partial input.
Reentrant session mutation during staging is refused.

The execution fingerprint binds capacities and vocabulary, not incarnation,
generation or current facts. Detachment restores the original fingerprint;
successful-solve history is not reset. Use a new session to attach again after a
successful transaction. The binding reserves an arena and fact-conversion storage
at attachment; the shared C transport allocates nothing during base reads,
replace/apply, queries and result release. JS arrays, encoded text and copied
outputs still allocate. Reference solving still recomputes IDB. The private wire
protocol is version 2, so these sources reject an older transport explicitly.

See [the shared contract](../../docs/binding-input-transactions.md). This feature
does not expose window adapters or change any public C/backend ABI.

## Manifests and environment I/O

Both runtimes accept the same manifest JSON, schema and permissions as Python:

```ts
const rules = engine.loadManifest({
  text: manifestJson,
  policies: [{ policyId: 'access.v1', source: policySource }],
}, { allowTestOnly: false, allowUndeclaredPolicyAtoms: false });
```

`default_profile` is `enforce`. Bundle entries match enabled manifest `policy_id`
values; the engine checks the source SHA-256 and query whitelist. In Node native,
`engine.loadManifest({ path: '/policies/manifest.json' })` delegates filesystem
loading to the same C loader as Python. WASM rejects path input with `UNSUPPORTED`;
the caller fetches the manifest and source bytes and supplies the memory bundle.
The existing C Advanced `manifest_text` API retains its historical schema.

Native registry declarations are shared by instances of the same addon/profile
across the process (including Node workers), like Python's native registry.
SMALL and LARGE embed separate engines with hidden symbols and independent
registries. Each WASM Engine normally owns a separate module and
registry. Register the same domain declaration in every Engine that uses it;
conflicting declarations are errors. The Node addon serializes entry into the
native library across workers through one recursive mutex per loaded profile.
There are no parallel native solves within a profile, even in separate workers.
Calls are synchronous after `Engine.create()`; use a worker to keep an event loop
responsive, and separate processes when parallel native solves are required.

## Build and verify

From the engine checkout, with CMake, a C11 compiler, Node headers and Emscripten:

```sh
for profile in small large; do
  tools/build_javascript_binding.sh native "$profile" build/javascript-package
  tools/build_javascript_binding.sh wasm "$profile" build/javascript-package
  MAELYS_PROFILE="$profile" node --test bindings/javascript/test/contract.mjs
done
```

Each adapter is compiled outside the source tree against an installed SDK of
that same build. The native static archive is built with PIC; no shared engine
library needs to be installed separately. `NODE_INCLUDE_DIR` selects a Node-API
header directory when it is not adjacent to the Node installation. The source
package contains no install script that downloads or compiles code.

Native builds hide engine symbols, export only the two Node-API registration
functions, and strip local/debug symbols. `tools/check_javascript_binary.py`
checks these properties plus the glibc requirements or macOS deployment target
of each actual binary before release archiving. A newer runner does not relax
these bounds. The macOS deployment target is pinned for all engine/adapter
objects and linking. Local validation of the target load command is not an
execution test on an older macOS installation.

Release packaging builds and tests with Node 22.23.3, fetched with its headers
from nodejs.org and checked against committed SHA-256 values. Development builds
use the selected Node and, if needed, fetch that exact version's headers and
verify its official checksum. Downloads happen during build, never installation.

## SDK compatibility and migration

Consumer API 2 identifies the incompatible-contract generation, not every
additive symbol. This binding requires the post-v0.14.0 SDK containing
`maelys_datalog_policy_load_manifest_buffer` and tests the
`MAELYS_DATALOG_HAS_MANIFEST_BUFFER` header feature guard at compilation.
Always use headers and libraries installed from the same revision and profile;
an API 2 SDK from v0.14.0 or an older release is insufficient. The first release
carrying this binding will supply that SDK and its matching prebuilds together. Python's
`Engine.load_manifest` still uses the stable file loader. Its CFFI surface also
declares the memory-bundle function to preserve complete stable-C coverage, and
checks the same header feature guard when building that extension.

The first release carrying this package retires `@maelys-dev/datalog-wasm` and
`MaelysPlayground` immediately. Only `@maelys-dev/datalog` is maintained and
published, with `/node` and `/wasm` entry points sharing this API. There is no
compatibility wrapper, old package alias, or fallback. Old tags and their
published archives remain immutable and usable by pinned historical consumers.

Consumers must migrate before upgrading. Replace `MaelysPlayground.create` with
`Engine.create`, `loadPolicy` with `loadInlineRuleset`, and move facts to an `Edb`.
Keep the `SolveResult` returned by a `Session` or `Ruleset.solve` for queries and
explanations; close the engine to release the full ownership tree. `query`
becomes `containsFact`, `enumerate` becomes `enumeratePredicateFacts`, and
`freeResult` becomes `result.close`. Close is now idempotent. Chained mutations
become separate statements. Register domains with `(name, predicates, atoms)`;
`Predicate` factories replace `PredKind` declarations. `limits`, `usage` and
`fingerprint` are properties on the corresponding owners.

Browser consumers promote the published package's `src/wasm.mjs`,
`src/core.mjs`, `src/wasm-transport.mjs`, selected `wasm/<profile>/engine.mjs`
and `engine.wasm`, and `src/index.d.ts` together, retaining relative paths.
Generate reference signatures from that exact declaration. Prepare runtime,
reference pages and receipt checks before publishing the cutover release; then
promote its immutable bytes together. Historical URLs may redirect or explain
migration, but must not present the retired API as the current binding. No
intermediate release continues both publication channels.
