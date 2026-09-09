<!-- SPDX-License-Identifier: MPL-2.0 -->
# Copyable extension starters

Use these minimal starters when you want to write your own extension, including
a proprietary one. Use the [MPL examples](../examples/README.md) to study working
implementations instead. These are two different resources with different licenses.

| Directory | Starting point | License of all files inside |
| --- | --- | --- |
| `frontend/` | Source language to validated public IR | MIT |
| `backend/` | Independent solver with explicit state ownership | MIT |
| `planner/` | Selection from safe join candidates | MIT |
| `filter/` | Pattern validation, cost and evaluation | MIT |

Each starter has the same `include/`, `src/`, `tests/`, `CMakeLists.txt`, `README.md`
and full `LICENSE`. They are newly authored scaffolding, not relicensed versions
of the MPL examples or solver. They contain no parsing, matching or solving
implementation: callbacks deliberately return `UNSUPPORTED`, and backend
capabilities remain zero. Successful registration does not mean a usable provider.

## Copy, build, implement

Install the engine SDK first:

```sh
cmake -S . -B build/sdk -DCMAKE_INSTALL_PREFIX="$PWD/build/sdk-install"
cmake --build build/sdk -j4
cmake --install build/sdk
```

Copy one complete starter directory, including its `LICENSE`, into your own
project. Installed copies are under `share/maelys-datalog/templates/`.
From that copied directory:

```sh
cmake -S . -B build -DMAELYS_SDK_PREFIX=/absolute/path/to/installed/sdk
cmake --build build
ctest --test-dir build --output-on-failure
```

The prefix must match your engine architecture and size profile. For shared-engine
linkage, use another build directory with `-DMAELYS_SDK_SHARED=ON`.
`tools/check_module_sdk.sh` verifies every installed starter outside the repository
with both linkage modes; native CI runs it for SMALL and LARGE.

Replace the callbacks and smoke expectations with your implementation and tests.
Choose your own names and semantic IDs. For a multi-kind package, combine the
typed descriptors in one public `maelys_datalog_extension_t` declaration; rename
entrypoints when combining starters. Read the MPL bundle example for its contracts,
but do not treat its implementation as MIT-licensed boilerplate.

The [conformance kit](../conformance/README.md) remains MPL and test-only. Its
helpers can test proprietary providers; copying its implementation into other
files carries its own licensing obligations. No conformance helper is embedded
in these MIT starters. Registration/error smoke tests are not a conformance proof.

## Licensing checklist

1. Preserve the starter's MIT copyright and permission notice in copies or
   substantial portions; your added implementation can use your chosen terms.
2. Keep an inventory of any additional copied code and its license. Merely using
   the SDK is different from copying an MPL implementation into your source file.
3. When distributing an engine-containing native or WASM artifact, retain the
   applicable notices and provide recipients access to the covered MPL source,
   including modifications, as required by MPL. Static/shared linking or a single
   WASM binary does not erase the source-file licensing boundary.
4. Keep proprietary implementations in their own repository/artifacts. The
   open-source reference engine must continue to work without them.

The engine, SDK headers, conformance kit and existing examples remain MPL-2.0;
only the four starter subdirectories carry this MIT exception. This guide itself
is MPL-2.0. Contributors must have rights to offer starter changes under MIT;
moving third-party MPL implementation code here is not a relicensing mechanism.

See the [MIT license](https://opensource.org/license/mit),
[Mozilla FAQ](https://www.mozilla.org/en-US/MPL/2.0/FAQ/) and the
[project boundary](../../docs/architecture/open-core.md#licensing-and-product-separation).
This checklist is engineering guidance, not a legal opinion on a particular product.
