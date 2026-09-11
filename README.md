# Maelys Datalog

[![CI](https://github.com/maelys-dev/maelys-datalog/actions/workflows/ci.yml/badge.svg)](https://github.com/maelys-dev/maelys-datalog/actions/workflows/ci.yml)
[![Version](https://img.shields.io/github/v/release/maelys-dev/maelys-datalog?color=775DFF)](https://github.com/maelys-dev/maelys-datalog/releases/latest)
[![License: MPL-2.0](https://img.shields.io/badge/license-MPL--2.0-00BFC0.svg)](LICENSE)

Maelys Datalog is a bounded, deterministic Datalog engine for embedded
policy decisions. The engine is implemented in C11 and can be embedded as a
native library or compiled to WebAssembly.

> **Early software:** the `0.x` releases are suitable for evaluation and
> integration experiments. Public APIs may still change before `v1.0.0`;
> that is what the `0.` in the version number says.

## Why Maelys

- deterministic semi-naive fixed-point evaluation;
- stratified negation with negative-cycle rejection;
- bounded memory profiles with stack-owned solver working state;
- static join planning and reproducible results;
- SHA-256 ruleset and complete policy-set identity, diagnostics, and decision
  receipts;
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

The active source and Why-true text contract is the unified
[`MAELYS-DATALOG-v2` specification](docs/specifications/maelys-datalog-v2/specification.md).
The profile token versions both surfaces together; the README does not
duplicate their grammar.

## Public API

New integrations use the opaque, installed C API:

```c
#include <maelys/datalog.h>
```

The legacy `include/maelys_datalog.h` umbrella remains available for alpha
compatibility. New modules must not depend on its internal engine types.

`maelys_datalog_policy_set_fingerprint()` returns a stable SHA-256 identity for
the exact executable bundle: ordered canonical rulesets, their domains and the
effective query whitelist. It is suitable for binding a reviewed authorization
plan to the policies that will be used when the plan is applied.

Complete integration guides and API documentation are available at
[datalog.maelys.dev](https://datalog.maelys.dev/).

## Open core and external modules

The MPL core remains usable on its own, with the reference solver and all three
standard string filters. The versioned [module SDK](include/maelys/datalog_module.h)
lets separately compiled modules provide new string filters and choose safe join
candidates. The [program SDK](include/maelys/datalog_program.h) adds explicit
language frontends lowering into a core-validated representation, and the
[backend SDK](include/maelys/datalog_backend.h) selects an independent solver per
session. All use public types; validation, input normalization, output limits,
query permissions and result ownership remain in the core.

The [extension envelope](include/maelys/datalog_extension.h) groups all four kinds
in one declaration. Register packages in an explicit context, seal its immutable
catalogue, then select frontends/backends by name. Used component identities enter
fingerprints. Existing global registration and direct selection APIs remain
compatible; missing capabilities fail without a fallback. See the
[context and migration guide](docs/architecture/extension-contexts.md).
Modules are trusted native code, not a sandbox. See the
[architecture and integration contract](docs/architecture/open-core.md) and the
[uniform standalone examples](sdk/examples/README.md), plus the
[compiler/backend integration guide](docs/architecture/compiler-backends.md).
Public-only examples implement a small arrow DSL and an independent naive
positive-Datalog solver. No proprietary code or Gitolite-compatible regex engine
is included. Python/JS bindings continue to use the reference backend.

## Repository layout

| Path | Purpose |
|---|---|
| `include/` | Opaque public API, module SDK, legacy umbrella and version macros |
| `src/core/` | Parser, registries, EDB, solver, audit, and decisions |
| `src/compiler/` | Shared validation, frontend builder and public program views |
| `src/runtime/` | Backend dispatch, result ownership and host-enforced output bounds |
| `src/backends/` | Built-in reference solver adapter |
| `src/registry/` | Module registration, identity and lifetime enforcement |
| `modules/standard/` | Standard string filters the engine ships, built against the public SDK like any third-party module |
| `sdk/examples/` | Four focused frontend/backend/planner/filter projects plus a composite bundle, using only the installed public SDK |
| `sdk/templates/` | Four MIT-licensed, copyable starters for independently implemented extensions |
| `sdk/conformance/` | Installed, test-only conformance helpers for all four extension contracts |
| `examples/` | Application examples using the engine; generated executables and debug bundles go under `build/examples/` |
| `build-support/` | Source manifests shared by native, WASM, fuzz and benchmark builds |
| `src/manifest/` | File and in-memory manifest loading |
| `bindings/wasm/` | WebAssembly-facing C boundary and its JavaScript wrapper |
| `bindings/python/` | Native Python binding |
| `tests/` | Native, Python, WASM, corpus, and fuzz tests |
| `tests/fixtures/` | Shared test material: the example domains every native test installs, and the out-of-tree SDK consumers |
| `docs/specifications/` | Normative, executable language and output-format specifications |
| `bench/` | Reproducible benchmarks and reports |

## Versioning

Maelys Datalog follows [Semantic Versioning](https://semver.org/). During the
`0.x` series, minor releases may contain breaking API changes — that is what
`0.` announces, so releases carry no `-alpha` suffix on top of it. The alpha
series ended at `v0.1.0-alpha.4`.

The canonical version is stored in [`VERSION`](VERSION). Release tags and the
website must use the same identifier. See [`CHANGELOG.md`](CHANGELOG.md) for
release notes.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Please
report suspected vulnerabilities privately as described in
[SECURITY.md](SECURITY.md).

## License

Copyright © 2026 David Bromberg.

Maelys Datalog is distributed under the [Mozilla Public License 2.0](LICENSE),
including the standard modules, SDK headers, conformance kit and working examples.
The four [extension starters](sdk/templates/README.md) are explicitly MIT-licensed
and may be adapted for proprietary implementations with their MIT notices retained.
The [MAELYS-DATALOG-v2 specification](docs/specifications/maelys-datalog-v2/specification.md)
is CC-BY-4.0 so that it can be quoted, translated and derived from; the ABNF
grammars and `.dl` corpus beside it are MIT, because an implementer copies them
into a parser or a test suite. [`LICENSING.md`](LICENSING.md) states each part.
Independently
authored external modules can use their own licenses, subject to the licenses
of any code they incorporate. The vendored `yyjson` parser retains its own MIT
license in `vendor/yyjson/LICENSE`. See [repository history](docs/repository-history.md)
for the MIT-era archive and the MPL transition.
