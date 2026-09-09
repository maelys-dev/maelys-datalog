# Compiler and solver extension contracts

The MPL core includes a complete standard Datalog frontend and reference solver.
The alpha C SDK has two independent extension points: source-language frontends
and execution backends. Neither needs private engine headers. The filter/planner
SDK remains available for narrower extensions; see [open core](open-core.md).
All four kinds can also be declared together and selected through an immutable
[extension context](extension-contexts.md), without changing the typed callbacks.

## Boundaries

| Stage | Open host responsibility | Extension responsibility |
| --- | --- | --- |
| Source → IR | Host selects domain and frontend; builder copies typed facts/rules | Parse a specific language and attach source locations |
| IR → validated program | Predicate/arity/constant checks, safe bindings, expression DAG, stratification and capacities | Cannot bypass validation or change the domain |
| Program → session | Snapshot, capability checks, identities and lifetimes | Prepare an algorithm-specific execution plan |
| EDB → result | Canonical input, bounded/deduplicated IDB, query permissions, atomic errors | Compute the complete fixed point and emit all derived facts |
| Explanation | Capability gate and result lease | Retain proof state and explain without re-solving |

The built-in parser retains the standard grammar and lowers through a private
parse-only entrypoint. Standard inline loads use the same frontend pipeline as
alternative languages. One common validation pass in
`src/compiler/maelys_datalog_validate.c` checks the whole IR, validates filter
programs once and assigns strata. The legacy parser wrappers use this same pass;
transient clause checkpoints preserve rejection of an entire OR expansion. When
a later clause fails to parse, the clauses parsed so far still receive their
clause-local checks, so the earliest error in source order is reported as
before; stratification remains a whole-program check after the last clause.
Identity is decided by the descriptor the host selected, never by a name the
callback claims: only `maelys_datalog_frontend_datalog()` itself keeps the
source-hash authority, and a copied descriptor that wraps the standard lowering
carries the extended identity like any other frontend.

The reference backend borrows the runtime's prepared session and already
materialized EDB through a private handle. There is one prepared session and one
input materialization per solve; external backends still consume canonical facts
through the public ABI. The runtime owns that session and releases it after the
backend state; retained results keep the existing lease. The reference algorithm
and its bounded proof snapshots remain unchanged in `src/core/`.

## Frontend contract

Include `maelys/datalog_program.h`. Implement a descriptor with ABI version,
exact struct size, name, semantic ID and `lower`. Select it explicitly with
`maelys_datalog_policy_load_frontend`; NULL selects standard Datalog. Existing
inline and manifest loading remain standard Datalog.

The callback receives source bytes and a callback-scoped opaque builder. Call
`maelys_datalog_program_add_fact` / `maelys_datalog_program_add_rule`; strings and
pattern bytes are copied synchronously. Zero-initialize IR structs, then fill
the fields applicable to each tagged variant. No source/builder pointer may be
retained. A failed builder operation is sticky: swallowing its status cannot
produce a successful partial load. Callback failure returns no policy.

The host-selected domain defines available predicates, roles, arities and atom
constants. A frontend cannot register symbols or predicates into that domain.
IR supports ground policy facts, positive/negative body atoms, comparisons,
integer expression DAGs and registered filters. Rule heads must be IDB; policy
facts must use POLICY_FACT predicates. The same binding safety, static type
checks, stratified negation and resource bounds apply to every frontend.

Limits include four terms per atom, eight body literals, 32 variables and
32 arithmetic nodes per rule. Variable IDs are local to a rule. Binary expression
operands must reference earlier nodes; cycles and invalid roots are rejected.
Each root's expanded tree must also fit 32 nodes so shared DAG edges cannot
trigger exponential recursive evaluation in the reference engine.
Integer source constants use the existing nonnegative 31-bit language range.
Runtime EDB integer values retain their existing int64 contract. Filters name a
registered provider and constant raw pattern; an optional supplied semantic ID
must match. Standard source escaping rules do not change.

Locations are 1-based line/column, or both zero if unavailable. Standard OR
expansion gives each resulting rule its source clause location. Public rule
access is zero-based; existing proof rule IDs are index + 1. This allows clients
to map proofs to source via `session_program` and `program_rule`. There is no
macro-expansion stack, end range, or new proof-text format in this ABI.

The example `sdk/examples/frontend/src/extension.c` implements only unary
`allow <- seed` implications and line comments. It is not a regex or Gitolite
parser. A richer domain frontend can lower its constructs into this IR, but a
new semantic feature outside the IR requires an explicit core/SDK evolution.
Avoid callbacks that change precedence or inject arbitrary grammar productions.

## Backend contract

Include `maelys/datalog_backend.h`. Backend ABI **v2** adds `explain_false`;
v1 descriptors are rejected and must be rebuilt. Populate a descriptor with
`prepare`, `solve`, `destroy_result`, `destroy`, and optional `explain_true` /
`explain_false` callbacks with their corresponding capability bits. Names are at most
63 bytes; semantic IDs at most 127. Names use lowercase letters/digits/underscore
and start with a letter. Semantic IDs also allow uppercase, dots and hyphens.
Use matching headers, ABI version, struct size and target architecture.

```c
maelys_datalog_session_options_t options = {
    .abi_version = MAELYS_DATALOG_BACKEND_ABI_VERSION,
    .struct_size = sizeof(maelys_datalog_session_options_t),
    .backend = my_backend(),
    .required_capabilities = MAELYS_DATALOG_CAP_EXPLAIN_TRUE,
};
maelys_datalog_session_t *session = NULL;
maelys_datalog_status_t status =
    maelys_datalog_session_create_ex(policy, 0, &options, &session);
/* Check status: unsupported capabilities never select another backend. */
```

The core computes required language capabilities from the validated program;
frontends do not declare them. The reference supports the full language,
Why-true and bounded Why-false (EXPLAIN_FALSE is bit 7). The independent `sdk/examples/backend/src/extension.c` implements positive
Datalog using full-scan fixed-point evaluation, with a cooperative work limit.
It rejects negation, comparisons, arithmetic, filters and explanations. It is a
conformance example, not an optimized product or a wrapper around the reference.

`prepare` receives an immutable program view. Accessors expose predicates, ground
policy facts and normalized rules using public types, plus profile-dependent
input/output capacities. Borrowed strings/patterns remain valid through
`destroy`. Sessions own snapshots: the caller may free the loaded policy first.
Descriptors and identity strings are copied per session; callback code must stay
loaded. Different backends can coexist without global registration.

`solve` receives canonical, deduplicated EDB inputs borrowed only for the callback.
Retain copies or algorithm-owned proof state when needed after return. Emit the
complete derived IDB, including non-query helpers, through `backend_emit`.
The host copies and deduplicates output, validates predicate roles/types/symbol
membership, enforces total and per-predicate capacities and sorts the result.
Derived symbols must already exist in the program/input vocabulary.
Ground policy facts and EDB belong to the host and must not be emitted as IDB.

A success status asserts complete materialization. The host cannot prove that
a native backend derived all and only logically justified facts. Differential
conformance is required before relying on a new algorithm, especially for
authorization decisions. A query-directed solver cannot return a partial answer
as success under this contract; incremental updates/streaming require another API.

Failures discard all output and destroy any returned result state, even if the
callback ignored a host error. Unknown callback statuses become INTERNAL.
`destroy` also runs on failed preparation; both destructors must accept NULL and
partially initialized state. One live result leases its session: free it before
another solve or session destruction. Sessions are confined to one caller at a
time; callbacks must not reenter the engine except program accessors/output APIs.

Queries honor QUERY flags and manifest whitelists. Query sees policy facts, EDB
and IDB; enumeration retains the existing derived-IDB-only behavior. Why-true
requires an advertised callback and must inspect retained state, not solve again.
Its buffer/required-size contract matches `maelys_datalog_result_explain_true_text`
in `datalog.h`. Backend capabilities are promises, not sandbox-enforced proofs.

`maelys_datalog_result_explain_false_text` has the same read-only contract. The
reference delegates to existing `maelys_datalog_explain_absent_solved_fact` with
128 candidate rules, 4,096 substitutions per rule, depth 10 and 16 diagnostics.
Its separate `MAELYS-DATALOG-WHY-FALSE-v1` text is part of the public contract.
It includes query, status, named limit hits (`none` or a comma-separated subset
of `candidate-rules`, `substitutions`, `depth`, `diagnostics`, `filter-cost`),
counters, substitutions, supports and obstacles (including filter semantic
identity). Variables print as `?N` and `binding=N`, where `N` is the rule-local
IR variable id that `maelys_datalog_program_rule` reports; the standard grammar
maps `A`–`Z` to 0–25 and anonymous variables to 26 and above, so the text never
depends on a frontend's surface names. A present query reports `not-applicable`;
an absent query reports `complete` or `truncated`. A bounded diagnostic is not an
exhaustive proof of non-derivability. The bounds are fixed by the reference in
backend ABI v2; letting the caller tune them means passing session options to
`prepare`, which is a later ABI revision. Unknown query symbols return NOT_FOUND
without mutating vocabulary. No source grammar or existing Why-true text changes.

Malformed frontend IR has a dedicated public load diagnostic code,
`MAELYS_DATALOG_DIAG_MALFORMED_PROGRAM`. Existing diagnostic values are preserved;
the code is appended. Parser syntax diagnostics keep their original codes.

## Budgets and shared filters

`backend_charge` is cooperative. Charge before bounded units of work; work units
are algorithm-specific, not comparable benchmarks or a wall-clock deadline.
Zero `work_limit` resolves to 1,048,576 host work units; a nonzero caller limit
requires WORK_LIMIT capability. The reference does not advertise that capability:
its existing depth, fact and filter bounds remain in force. The naive example
charges rule scans, join candidates and duplicate comparisons. Host emission
caps always apply, regardless of advertised WORK_LIMIT.

Backends supporting filters can call `backend_filter` using the program's
provider name, semantic ID and exact declared pattern. The host shares registered
provider semantics and charges its filter evaluation/cost budget before dispatch.
Unknown versions, undeclared patterns, invalid outputs and budget failures are
fatal, not false matches. The test fixture exercises this service independently
of the reference backend. A full backend remains responsible for using it at the
logically correct places in evaluation.

All callbacks are trusted deterministic native code. Unlike allocation-free
filter/planner callbacks, frontends/backends may allocate bounded owned state.
No arbitrary native loop, memory write, I/O or dishonestly declared work bound can
be contained by this C ABI. Untrusted modules need process/WASM isolation.

## Identity and compatibility

Existing policy/session fingerprints retain their authority meaning and standard
source-hash compatibility. Alternative frontends add name, semantic ID and source
hash to their policy identity. Do not use a legacy source hash alone as a compiled
program or multi-backend cache key.

`program_fingerprint` separately hashes typed, length-framed program data,
including policy/domain, predicate schema, declared atoms, normalized rules,
source locations, filter semantics and query restrictions. It is an identity of
this compiled contract, not a canonical equivalence test between source programs.
It is computed once at ruleset finalization after validation, then read from the
immutable program cache. Parsing/builder mutations invalidate that cache; these
private metadata fields are not fingerprint inputs.
`session_execution_fingerprint` additionally binds backend name/semantic ID,
required capabilities, resolved work limit and SMALL/LARGE profile. Neither
fingerprint includes the runtime EDB; result caches must bind input identity too.
Neither attests machine code. Bump semantic IDs when behavior or work accounting
changes, and use artifact provenance separately.

The new dispatch API is native C. Existing low-level, Python and stock WASM/JS
entrypoints continue using the reference solver; no new language-binding backend
selector is claimed. Compiler validation still builds and runs in those targets.
The opaque C API is source-compatible, but legacy consumers using private/POD
layouts must rebuild because source-location/frontend metadata changed the
internal ruleset layout. IR structs are views, not a wire or serialization format.

## Verification and product split

CMake tests link both examples against static and shared libraries. The installed
SDK check compiles them and the integration consumer with only installed public
headers. Tests cover both frontends × both backends, invalid IR, capabilities,
lifetimes, failures, work limits, shared filters, identities, IR round trips and
20 generated recursive graphs compared against independent transitive closure.
Pipeline regressions compare exact-byte transcript SHA-256 goldens captured at
PR #4, before the single-pass refactor, for both SMALL and LARGE. They include
authority/program/execution fingerprints and why-true/why-false text, policy
facts, OR, negation, arithmetic, filters, recursion, a third-party frontend and
reversed EDB inputs. Native test-only thread-local counters assert one validation,
one fingerprint computation, one preparation and one materialization per solve.
`make bench-pipeline` measures 2,000 solves and asserts the materialization count;
its timing is informational, not a speed guarantee. No counters enter production
builds or the public ABI.

```sh
make test
cmake -S . -B build/cmake
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
bash tools/check_module_sdk.sh "$PWD/build/cmake"
```

The compiler/backend implementation, public headers and working examples stay
MPL-2.0. Separate [MIT starters](../../sdk/templates/README.md) provide copyable,
unimplemented scaffolding without relicensing those examples. Future
independently authored proprietary frontends, optimized solvers or regex providers
can be developed as separate private artifacts against these public interfaces.
This repository contains no private implementation or paywall in the reference
engine. Copying/modifying MPL implementation files is not equivalent to an
independent SDK implementation; the [licensing boundary](open-core.md#licensing-and-product-separation)
and applicable contribution rights still govern that distinction.
