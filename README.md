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
- stratified distinct `count` with explicit grouping and empty-group zero (0.6.0);
- integer `min`, `max` and checked `sum`, using the same grouping syntax (0.7.0);
- bounded memory profiles with stack-owned solver working state;
- static join planning and reproducible results;
- SHA-256 ruleset and complete policy-set identity, diagnostics, and decision
  receipts;
- native C API plus Python and JavaScript/WASM bindings;
- fail-closed loading and solving behavior.

The solver does not allocate heap memory while evaluating rules. A successful
solve creates one caller-owned result object that must be released through the
public API.

## Count values per group (0.6.0)

Register the ordinary predicates in your domain, then use the same language
through C, Python or WebAssembly:

```datalog
errors(Id, Service) :- log(Id, Service, "error").
error_count(Service, N) :- service(Service), count(Id, errors(Id, Service), N).
alert(Service) :- error_count(Service, N), N >= 10.
```

A registered service with no errors gets zero. Give each log occurrence a unique
ID: `count` counts distinct typed IDs, not duplicate input tuples. Sources are
fully evaluated before counting; recursion through an aggregate is rejected.
The reference engine recomputes each supplied snapshot. See the
[aggregate contract](docs/specifications/maelys-datalog-v2/aggregates.md) for
binding, capability negotiation and explanation semantics.

## Retain the last N events (unreleased)

The native [`datalog_window.h` adapter](docs/architecture/last-n-window.md) adds
integer occurrence IDs and recomputes the last N accepted events. Expiration,
insertion and result replacement commit together: a rejected event preserves
the old window and result. It uses caller-provided input storage and two borrowed
sessions; there are no adapter allocations or language changes. This reference
path provides transactional behavior before incremental maintenance.

## Build an explanation once

### Reuse a session workspace (0.5.0)

Opt in when creating a reference session; ordinary sessions reserve no explanation
workspace. Both kinds share one allocation, sized to the larger profile bound.
The existing text functions then prepare once for measure/write/retry of the
same query, with **zero reference-engine allocator calls after session creation**.
The caller still owns the output text buffer. This does not claim zero Python,
custom-filter, compilation or initialization allocations, or zero rendering cost.

```c
maelys_datalog_session_config_t *config = NULL;
maelys_datalog_session_t *session = NULL;
maelys_datalog_status_t rc = maelys_datalog_session_config_create(&config);
if (rc != MAELYS_DATALOG_STATUS_OK) return rc;
rc = maelys_datalog_session_config_set_explanation_workspace(
    config, MAELYS_DATALOG_EXPLAIN_TRUE | MAELYS_DATALOG_EXPLAIN_FALSE);
if (rc == MAELYS_DATALOG_STATUS_OK)
    rc = maelys_datalog_session_create_configured(policy, 0, config, &session);
maelys_datalog_session_config_free(config);
if (rc != MAELYS_DATALOG_STATUS_OK) return rc;
/* Solve, then use result_explain_true_text / result_explain_false_text as before.
 * result_free also releases the internal cache; session_free releases its space. */
```

Applications can instead use `session_config_set_explanation_storage(config,
kinds, storage, bytes)`: the application keeps aligned memory alive and exclusive
until the session is freed. It may use the per-kind session bound introduced in
0.4.1 to budget that memory, taking the maximum for both kinds. Too little space
returns `STORAGE_TOO_SMALL` at creation; overlapping live session ranges return
`INVALID_STATE`. There is no allocating fallback and no automatic secure erasure.
The copied config does not extend the application's buffer lifetime. Set workspace
mask zero to disable the option; setters replace the previous mode and mask.

The cache compares result generation, kind, predicate, arity and typed values;
query strings are not retained. Another valid direct-text explanation replaces
it. Membership queries, explicit prepared handles and a too-small output buffer
do not evict it. Invalid requests preserve it; failed preparation leaves it empty.
Only selected kinds use the convenience path; unselected kinds return
`UNSUPPORTED`. A result release discards its cache even if only the size was
requested. Caller-owned prepared handles must still be released first. Session
use remains single-threaded; the option changes neither fingerprint nor proof text.
ABI 3 has no custom-backend upper-bound callback: only the canonical reference
backend supports this mode, not a copied descriptor.

### Explicit caller-owned prepared handles

The opaque facade in `<maelys/datalog.h>` supports explanations in caller-owned
memory. Why-true describes a retained witness; Why-false performs a bounded
diagnostic search over the solved state. Neither runs the solver again.

```text
live result
  └─ prepare explanation once in caller storage
       ├─ text_size: read the cached length
       ├─ write_text: format into a caller buffer (repeatable)
       └─ release explanation
  └─ release result → session can serve the next request
```

1. Call `maelys_datalog_result_explanation_storage_requirements(result, kind,
   &bytes, &alignment)`. This only reports memory requirements, not text length.
   Supply an aligned arena of at least that size; requirements depend on the
   live result, backend, kind and library build, not a public struct layout.
2. Call `maelys_datalog_result_prepare_explanation(...)` with that arena and
   a ground query. It builds an opaque `maelys_datalog_prepared_explanation_t`.
3. Read the exact text length with `maelys_datalog_prepared_explanation_text_size`.
   It excludes the terminating NUL. Provide at least `length + 1` bytes to
   `maelys_datalog_prepared_explanation_write_text`. A fixed output buffer works
   too: an insufficient buffer is rejected without partial text.
4. Release the explanation with `maelys_datalog_prepared_explanation_release`
   before freeing the result. Release does not free or securely erase the arena;
   its owner may reuse it. One result can have several prepared explanations.

The reference path makes **zero engine allocator calls**, including Why-false
search scratch, after the caller has supplied the storage. It still uses bounded
stack locals. This is not a zero-allocation claim about Python, custom filters,
compilation or session initialization. An explanation whose search is `truncated`
is different from an output buffer that is too small; allocating a larger text
buffer does not increase the search bounds.

[The installed public-consumer example](tests/fixtures/public_api_consumer.c)
uses a fixed aligned arena and output buffer, checks their sizes and demonstrates
the result lease. It is compiled and run against the installed C11/C++17 SDK.
The existing `result_explain_true_text` / `result_explain_false_text` calls remain
allocating convenience wrappers when no session workspace is configured. Backend authors must migrate to
**backend ABI 3**; the consumer facade and Datalog language versions do not change.

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

Declaration initializers, added in 0.7.0, keep common predicate roles explicit:

```c
static const maelys_datalog_public_predicate_t predicates[] = {
    MAELYS_DATALOG_EDB("seed", 1),
    MAELYS_DATALOG_IDB("hidden", 1),
    MAELYS_DATALOG_IDB_QUERY("allow", 1),
};
```

These C/C++ macros initialize ordinary declarations without allocating or
registering anything. `IDB_QUERY` means `IDB | QUERY`: a derived predicate also
exposed for queries. Names and arities are still validated by domain registration.
All six accepted combinations have an initializer:

| Origin | Without public queries | With public queries |
| --- | --- | --- |
| Request input | `MAELYS_DATALOG_EDB` | `MAELYS_DATALOG_EDB_QUERY` |
| Rule-derived fact | `MAELYS_DATALOG_IDB` | `MAELYS_DATALOG_IDB_QUERY` |
| Trusted policy-source fact | `MAELYS_DATALOG_POLICY_FACT` | `MAELYS_DATALOG_POLICY_FACT_QUERY` |

Every initializer takes `(name, arity)`. `QUERY` is a permission, not an origin:
a query-only declaration is rejected when loading the policy's predicate
registry, not by the initializer or the domain-registration call. Adding it does not allow a request to
inject policy facts or derived facts. The ordinary struct initializer remains
available. The existing C11 `MAELYS_DATALOG_QUERY(result, ...)` is different:
it executes a membership query; it does not declare a predicate.

The C11 fact builders introduced in 0.4.0, included automatically from the separate
installed `<maelys/datalog_builders.h>`, simplify input without
changing the ABI. Given a successfully initialized `edb` and a diagnostic:

```c
maelys_datalog_status_t rc = MAELYS_DATALOG_ADD_FACT(
    edb, &diagnostic, "owns", "alice", "roadmap.pdf");
/* Check rc exactly as for maelys_datalog_input_edb_add_fact(). */
```

The macro infers zero to four terms: strings become symbols, representable
integers become signed 64-bit integers, and `_Bool` becomes a boolean.
`MAELYS_DATALOG_BOOL(value)` requests boolean semantics explicitly: C11 `true`
and comparison expressions otherwise have integer type. Arguments are evaluated
once, with no evaluation-order guarantee and no extra allocation. Invalid
types fail compilation; out-of-range integers return an input diagnostic before
any insertion. C++ and FFI consumers continue using the typed-value API.
The [public header](include/maelys/datalog.h) specifies the complete contract.

For several facts, submit one atomic batch instead of independent additions:

```c
rc = MAELYS_DATALOG_ADD_FACTS(
    edb, &diagnostic,
    MAELYS_DATALOG_FACT("user", "alice"),
    MAELYS_DATALOG_FACT("owns", "alice", "roadmap.pdf"),
    MAELYS_DATALOG_FACT("blocked", "mallory"));
/* Check rc: failure appends none of these facts; older facts remain. */
```

`FACT` builds a checked descriptor without inserting or copying strings.
`ADD_FACTS` counts the descriptors and calls `input_edb_add_facts` once, using
automatic temporary arrays and the same conversions as `ADD_FACT`. Strings
are copied by the native batch call before it returns. Arguments are evaluated
once, in unspecified order. The macro requires at least one `FACT`; for large,
dynamic or empty batches, use `maelys_datalog_input_edb_add_facts(edb, facts,
count, &diagnostic)` with an ordinary `maelys_datalog_public_fact_t` array.
`FACT` is not a public-fact initializer. Multiple independent `ADD_FACT` calls
do not roll back earlier successful calls if a later one fails.

After solving, the C11 query shortcut uses the same conversions:

```c
int present = 0;
rc = MAELYS_DATALOG_QUERY(result, &present, "allow", "alice", "roadmap.pdf");
/* Check rc first: present == 0 means absence only when rc is OK. */
```

The explicit typed API remains available, including in C++17. A symbol
initializer reduces boilerplate without hiding the values array:

```c
const maelys_datalog_public_value_t terms[] = {
    MAELYS_DATALOG_SYMBOL("alice"),
    MAELYS_DATALOG_SYMBOL("roadmap.pdf"),
};
rc = maelys_datalog_result_query(result, "allow", terms, 2u, &present);
```

`MAELYS_DATALOG_SYMBOL` is an initializer, not an expression. It borrows its
string without copying or allocating. These conveniences were added in 0.4.0;
existing typed declarations and functions are not deprecated.

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
