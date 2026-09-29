# @maelys-dev/datalog

One JavaScript/TypeScript consumer API, with two implementations:

- `@maelys-dev/datalog/node`: a native Node-API addon with the engine compiled in.
- `@maelys-dev/datalog/wasm`: WebAssembly for browsers, module workers, or Node.

The root import selects native Node in Node and WASM with the `browser` export
condition. Browser code without a bundler uses the ESM WASM entry point. No
runtime silently falls back to another backend. The native artifact targets
Linux x64, Linux arm64 and macOS arm64, with Node-API 8; CI exercises Node 22 and
24. Both runtimes expose SMALL and LARGE engine profiles.

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

Native registry declarations are process-wide (including Node workers), like
Python's native registry. Each WASM Engine normally owns a separate module and
registry. Register the same domain declaration in every Engine that uses it;
conflicting declarations are errors. The Node addon serializes entry into the
native library across workers. Calls are synchronous after `Engine.create()`;
use a worker for expensive solves to keep an event loop responsive.

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
