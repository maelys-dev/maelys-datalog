# Maelys Datalog

[![CI](https://github.com/maelys-dev/maelys-datalog/actions/workflows/ci.yml/badge.svg)](https://github.com/maelys-dev/maelys-datalog/actions/workflows/ci.yml)
[![Version](https://img.shields.io/badge/version-v0.1.0--alpha.1-775DFF)](https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.1)
[![License: MIT](https://img.shields.io/badge/license-MIT-00BFC0.svg)](LICENSE)

Maelys Datalog is a bounded, deterministic Datalog engine for embedded
policy decisions. The engine is implemented in C11 and can be embedded as a
native library or compiled to WebAssembly.

> **Alpha software:** `v0.1.0-alpha.1` is suitable for evaluation and
> integration experiments. Public APIs may still change before `v1.0.0`.

## Why Maelys

- deterministic semi-naive fixed-point evaluation;
- stratified negation with negative-cycle rejection;
- bounded memory profiles with stack-owned solver working state;
- static join planning and reproducible results;
- SHA-256 policy identity, diagnostics, and decision receipts;
- native C API plus Python and JavaScript/WASM bindings;
- fail-closed loading and solving behavior.

The solver does not allocate heap memory while evaluating rules. A successful
solve creates one caller-owned result object that must be released through the
public API.

## Build and test

Requirements: a C11 compiler, `make`, and optionally CMake 3.16 or newer.

```sh
git clone https://github.com/maelys-dev/maelys-datalog.git
cd maelys-datalog
make test
```

To build with CMake:

```sh
cmake -S . -B build/native
cmake --build build/native
```

The default `SMALL` profile supports 1,024 EDB facts. Configure
`-DMAELYS_DATALOG_PROFILE_LARGE=ON` for the `LARGE` profile with 2,048 EDB
facts.

## Public API

Include the umbrella header:

```c
#include "include/maelys_datalog.h"
```

The header exposes the release identifier through
`MAELYS_DATALOG_VERSION_STRING`.

Complete integration guides and API documentation are available at
[datalog.maelys.dev](https://datalog.maelys.dev/).

## Repository layout

| Path | Purpose |
|---|---|
| `include/` | Public umbrella API and version macros |
| `src/core/` | Parser, registries, EDB, solver, audit, and decisions |
| `src/manifest/` | File and in-memory manifest loading |
| `src/wasm/` | WebAssembly-facing C API |
| `bindings/python/` | Native Python binding |
| `js/` | JavaScript playground wrapper |
| `tests/` | Native, Python, WASM, corpus, and fuzz tests |
| `bench/` | Reproducible benchmarks and reports |

## Versioning

Maelys Datalog follows [Semantic Versioning](https://semver.org/). During the
`0.x` series, minor releases may contain breaking API changes. Prereleases use
tags such as `v0.1.0-alpha.1`.

The canonical version is stored in [`VERSION`](VERSION). Release tags and the
website must use the same identifier. See [`CHANGELOG.md`](CHANGELOG.md) for
release notes.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Please
report suspected vulnerabilities privately as described in
[SECURITY.md](SECURITY.md).

## License

Copyright © 2026 David Bromberg.

Maelys Datalog is distributed under the [MIT License](LICENSE). The vendored
`yyjson` parser retains its own MIT license in `vendor/yyjson/LICENSE`.
