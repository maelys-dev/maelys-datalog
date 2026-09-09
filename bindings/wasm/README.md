# JavaScript / WebAssembly binding

This directory contains both sides of the binding:

- `maelys_datalog_wasm.c` and `.h`: C entrypoints exported by the dynamic WASM
  build, including domain/input construction, solving, queries and explanations.
- `maelys_playground.js`: the JavaScript wrapper over those entrypoints, usable
  from Node and a browser. Despite its historical name, it is not a browser UI.

The engine stays in `src/`; compiling it to WASM does not make it a binding.
This C boundary still uses internal engine types. Moving to the opaque public
facade would be a separate refactor, not an effect of this directory layout.

## Build and test

From the repository root, with Emscripten and Node available:

```sh
for profile in small large; do
  make -f Makefile.wasm maelys_datalog_dynamic.js \
    WASM_PROFILE="$profile" EM_CACHE="$PWD/build/emscripten-cache"
  for script in tests/wasm/*.mjs; do
    MAELYS_WASM_PROFILE="$profile" node "$script"
  done
done
```

Outputs stay in `build/wasm/` and `build/wasm-large/`; no generated `.js`, `.wasm`,
object or debug files belong in this binding source directory.
Native boundary tests are in `tests/test_maelys_datalog_wasm_builder.c`; the
full commands, including sanitizers, are in [the validation matrix](../../docs/validation.md).

## Source imports and distribution

Repository tests import `../../bindings/wasm/maelys_playground.js`. For example,
from a module in `tests/wasm/`:

```js
import playgroundPkg from '../../bindings/wasm/maelys_playground.js';
import createModule from '../../build/wasm/maelys_datalog_dynamic.js';
const { MaelysPlayground } = playgroundPkg;
const wasmUrl = new URL('../../build/wasm/maelys_datalog_dynamic.wasm', import.meta.url).href;
const pg = await MaelysPlayground.create(createModule, wasmUrl);
```

Release archives still place `maelys_playground.js`, `maelys_datalog_dynamic.js`
and `maelys_datalog_dynamic.wasm` together at the archive root. The packaging
script reads the wrapper here without changing any distributed basename or API.
If TypeScript declarations are added, the preferred source location is
`bindings/wasm/maelys_playground.d.ts`; none is supplied currently.

See [release engineering](../../docs/release-engineering.md) for the pinned
release toolchain. The JavaScript binding does not yet expose extension-context
selection. Same-module C extension examples are tested independently through
`tools/check_wasm_extensions.sh`; there is no dynamic extension loader.
