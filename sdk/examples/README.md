# Extension examples

Five standalone C11 projects use the same layout, declaration envelope, build
commands and test-only conformance kit. None includes private engine headers.

These working examples are **MPL-2.0**, not permissive copy-and-close templates.
For a proprietary implementation, start with the [MIT starters](../templates/README.md)
and write your own logic. Copying or modifying these example files retains the
applicable MPL obligations; using the public SDK alone is a different operation.

| Project | Component | Deliberate scope |
| --- | --- | --- |
| `filter` | `exact_match` | Byte-for-byte string equality |
| `planner` | `selective` | Choose one host-provided safe join candidate |
| `frontend` | `arrow` | Unary implications, not Gitolite |
| `backend` | `naive` | Small positive Datalog fixed point, not production performance |
| [`bundle`](bundle/README.md) | `permit` frontend + `exact_match` filter | Authorization DSL using its companion filter through the IR |

Each directory contains `include/extension.h`, `src/extension.c`,
`tests/conformance.c`, `CMakeLists.txt`, `README.md` and `LICENSE`.
The entrypoint `example_<kind>_extension()` returns a declaration by value;
registration does not run initialization callbacks. Typed interfaces remain
different because their contracts are different.

## Build every example

From the engine checkout (choose SMALL or add
`-DMAELYS_DATALOG_PROFILE_LARGE=ON` when configuring the engine):

```sh
cmake -S . -B build/sdk -DCMAKE_INSTALL_PREFIX="$PWD/build/sdk-install"
cmake --build build/sdk -j4
cmake --install build/sdk
for kind in frontend backend planner filter bundle; do
  cmake -S "sdk/examples/$kind" -B "build/example-$kind" \
    -DMAELYS_SDK_PREFIX="$PWD/build/sdk-install"
  cmake --build "build/example-$kind"
  ctest --test-dir "build/example-$kind" --output-on-failure
done
```

Use a separate build directory with `-DMAELYS_SDK_SHARED=ON` for shared-engine
linkage. The extension itself remains a statically linked library; this is not
a dynamic loader. `bash tools/check_module_sdk.sh build/sdk` additionally copies
each project outside the repository and tests both linkage modes against a fresh
installation. No source-tree include fallback is allowed.

## Common conventions

- Zero-initialize declarations and IR; set exact ABI version and struct size.
- Return `maelys_datalog_status_t`; do not print or terminate from callbacks.
  Tests alone print failures and return nonzero.
- Keep names stable; change semantic IDs whenever observable semantics change.
- Treat callback arguments as borrowed; respect each typed ownership contract.
- Keep unsupported features explicit. Never silently fall back or approximate.
- Add positive, negative, boundary and provider-error fixtures before adding a feature.

See the [context and migration guide](../../docs/architecture/extension-contexts.md)
and the [conformance kit](../conformance/README.md).
