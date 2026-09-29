# @maelys-dev/datalog

One JavaScript/TypeScript consumer API, with two implementations:

- `@maelys-dev/datalog/node`: a native Node-API addon with the engine compiled in.
- `@maelys-dev/datalog/wasm`: WebAssembly for browsers, module workers, or Node.

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
additive symbol. This binding requires the post-v0.13.0 SDK containing
`maelys_datalog_policy_load_manifest_buffer` and tests the
`MAELYS_DATALOG_HAS_MANIFEST_BUFFER` header feature guard at compilation.
Always use headers and libraries installed from the same revision and profile;
an API 2 SDK from an older release is insufficient. The first release carrying
this binding will supply that SDK and its matching prebuilds together. Python's
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
