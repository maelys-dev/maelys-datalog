# Selective planner

## Contract

Prefers more bound terms, then the host default score. It only returns an index into the safe candidates supplied by the host; it cannot rewrite rules or change semantics. Register it and select its name when sealing the context.

The public entrypoint `example_planner_extension()` returns the common declaration.
Only public SDK headers are included; no engine source is needed to build this project.

## Build and test

Install the engine SDK first. From this directory, replace the prefix with its
absolute installation path; these commands are identical in all examples:

```sh
cmake -S . -B build -DMAELYS_SDK_PREFIX=/absolute/path/to/sdk-install
cmake --build build
ctest --test-dir build --output-on-failure
```

For shared-engine linkage use a separate directory and add
`-DMAELYS_SDK_SHARED=ON`. The provider is still statically linked into the test
executable; no runtime loader is used.

## Conformance

Deterministic in-range choices for one to three candidates, preferred choice, invalid arguments, registration and explicit selection.

Extend `tests/conformance.c` with your own feature and boundary fixtures.
The installed test-only kit checks supplied cases, not arbitrary semantic
equivalence or native-code safety. Run sanitizers as well.

## Errors and ownership

Callbacks return `maelys_datalog_status_t`; only tests print or exit.
Use exact ABI/struct sizes and update the semantic ID when behavior changes.
Respect borrowed callback inputs and the typed descriptor's lifetime rules.
The context copies descriptors and names, not executable code.

## Layout and license

`include/extension.h` declares the envelope; `src/extension.c` implements it;
`tests/conformance.c` exercises it. The standalone CMake build accepts only an
installed SDK and its conformance kit.

This example is MPL-2.0; see `LICENSE`.
