# Changelog

All notable changes to Maelys Datalog are documented in this file.

The project follows [Semantic Versioning](https://semver.org/) and uses the
format described by [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Fixed

- Low-level domains now install their declared `atoms` after a successful
  `install_predicates` callback, as table-based domains already do. Callback
  failures stop installation; atom installation errors are propagated. Previously
  the callback path returned early and silently ignored the atom table. Stable
  consumer types and backend ABI 3 layouts are unchanged.

## 0.9.0 — 2026-09-23

Multi-fact event windows: one group may contribute several facts, shared
facts survive until their last contributing group expires. Consumer API v1,
backend ABI 3 and program ABI 1 layouts remain unchanged.

### Added

- Native multi-fact event windows in `<maelys/datalog_group_window.h>`, with
  caller-owned fixed storage and separate group, raw contribution, unique-fact
  and text capacities. Group IDs stay outside facts; shared facts survive until
  their last contributing group expires. Empty groups advance retention normally.
  Boolean normalization and typed union preserve existing Datalog semantics.
  The raw contribution capacity is capped at `MAX_EDB_FACTS` (1,024 in SMALL,
  2,048 in LARGE); every retained duplicate consumes a contribution slot,
  even when the union contains very few unique facts.
- Atomic publication of the retained groups, union, result and cursor using two
  borrowed sessions. Rejection preserves committed bytes and views; closed
  handles reject before accessing returned sessions. Allocation guards, generated
  independent FIFO/set oracles and installed static/shared consumers cover both
  SMALL and LARGE. Existing single-fact windows and backend ABI 3
  remain unchanged.

## 0.8.0 — 2026-09-23

A bounded last-N event window over the public facade, and the read-only
accessors it needed. Consumer API v1, backend ABI 3 and program ABI 1
layouts remain unchanged.

### Added

- Native last-N event windows in `<maelys/datalog_window.h>`: caller-owned bounded
  storage, two borrowed sessions, generated integer occurrence IDs, complete
  snapshot recomputation and atomic expiry/insertion/result publication. Rejection
  preserves the previous result and cursor. The adapter allocates nothing;
  session creation and existing explanation/provider exceptions remain separate.
  Closing clears borrowed references and rejects further operations while the
  caller arena remains alive and unmodified, including after sessions are freed.
- Constant-time `input_edb_text_usage` and `window_text_usage` report interned
  predicate/symbol bytes including NULs and their configured text capacity.
  Window usage describes the committed input bank, remains unchanged on failure,
  and can decrease after expiry; candidate/session memory is not included.
- Read-only `input_edb_view` for ordered raw entries. The window adapter consumes
  only the public facade and ships in the installed native SDK. Existing ABI
  layouts and language syntax are unchanged; Python/JS window bindings and
  incremental maintenance are not introduced.

## 0.7.1 — 2026-09-22

A named zero for the loading permissions, and a WebAssembly failure that
says which bound it met. No behavior, export, identity or ABI change.

### Added

- `MAELYS_DATALOG_PUBLIC_ALLOW_NONE` names the absence of optional manifest-loading
  permissions. Its value is permanently `0u`; existing literal-zero calls,
  behavior, exported symbols and ABI layouts are unchanged.

### Fixed

- A failed `solve` through the WebAssembly binding now reports why it stopped.
  `maelys_datalog_wasm_solve` returned the solver's status but never recorded a
  diagnostic, so every solve-time failure reached callers as a bare return code
  and a capacity ceiling could not be told from a depth ceiling or a defect. The
  message is the category name the native public API reports for the same
  failure, the hint carries the observed count and the limit where the solver
  provides them, and state rejections name the missing precondition. No public
  signature, export or identity changes.

## 0.7.0 — 2026-09-22

Three additive numeric aggregates and a read-path optimization.
Consumer API v1, backend ABI 3 and program ABI 1 layouts remain unchanged.

### Added

- Public stratified integer `min`, `max` and `sum`, with the same contextual
  syntax and variable scope as `count`. Empty extrema fail; empty sums are zero.
  Sum includes each distinct complete source fact once, preserving separate
  events of equal value. Matching non-integers and sums above 2147483647 fail
  atomically with `INVALID_FIELD`. Existing bounded storage and session leases
  remain in force; no engine allocations are added to snapshot evaluation.
- Independently negotiated `CAP_MIN`, `CAP_MAX`, `CAP_SUM` and public IR kinds
  `IR_MIN=6`, `IR_MAX=7`, `IR_SUM=8`. Python-next exposes matching capabilities.
  `CAP_AGGREGATES` remains count-only; `CAP_LANGUAGE` and ABI layouts are unchanged.
  `CAP_ALL` becomes 4095. Providers must handle or reject new enum alternatives.
- Snapshot explanations append `min`/`max`/`sum` premises, corresponding mismatch
  obstacles and `min-empty`/`max-empty` obstacles. Native premise kinds append
  6..8 and obstacle kinds 7..11; exhaustive-switch consumers need updating.
  Existing programs' identities, explanation bytes and native struct layouts
  remain unchanged. This additive language feature requires a minor release.

### Changed

- Read-only symbol lookup probes the existing hash index when the table has
  more entries than the lookup text has bytes, retaining the scan for short
  tables/long keys. Symbol identities, storage sizes and valid-table lookup
  results remain unchanged; no allocations or new index storage are introduced.

## 0.6.0 — 2026-09-21

An additive language, IR and explanation extension.
Consumer API v1, backend ABI 3 and program ABI 1 layouts remain unchanged.

### Added

- Public stratified distinct count: `count(Id, relation(...), N)` in rule bodies,
  with explicit group keys, typed projection, empty-group zero, and bounded
  allocation-free reference evaluation. A source relation may be recursive;
  recursion through an aggregate is rejected. Joins and filters use auxiliary
  relations. Ordinary predicates named `count` retain their meaning.
- Public `CAP_AGGREGATES` and `IR_COUNT`, using existing ABI 3 descriptors and
  program ABI 1 rule layouts. Backends must opt in; the historical
  `CAP_LANGUAGE` mask remains unchanged. Python-next exposes
  `Capability.AGGREGATES`. Streaming, windows and incremental updates are not
  introduced by this feature.
- Count snapshot observations in Why-true, and `count-mismatch` obstacles in
  Why-false, including prepared/caller-owned explanations. Existing programs'
  explanation bytes and identities are preserved.

### Migration

- Exhaustive public IR switches must handle or explicitly reject
  `MAELYS_DATALOG_IR_COUNT` (5). Native explanation integrations must account for
  `MAELYS_DATALOG_EXPLANATION_PREMISE_COUNT` (5) and the Why-false obstacle
  `MAELYS_DATALOG_WHY_FALSE_OBSTACLE_COUNT_MISMATCH` (6); text consumers must
  recognize the corresponding count observation and `count-mismatch` alternative.
  Existing enum numbers are unchanged. `AGGREGATES` is optional, and programs
  without aggregates emit none of these count alternatives.

### Performance

- Native session input materialization uses a bounded fact index instead of
  scanning all previously inserted facts for every duplicate check above 32
  distinct facts. Smaller sets retain the scan; fact 33 backfills the index. The index
  reuses symbol-sort scratch after interning: no session-size increase, new
  allocation, public layout or ABI change. Capacity checks still precede
  deduplication, and final sorting preserves canonical result identities.

### Tooling

- The manual revision benchmark now measures public `session_solve_edb` with
  inert and deriving policies, both profiles, integer/symbol values, input
  permutations, duplicates and strided values up to the global EDB bound.
  Full result checks and A/A noise floors precede any performance conclusion.
  Optional Callgrind and neutral-link-layout diagnostics isolate the historical
  LARGE/1024 or LARGE/2048 solve payload. Reports show the recorded CPU model and
  system in their header, including an explicit unknown value for older artifacts;
  generated evidence remains in run artifacts.

- Adopt maelys-release v0.60.0 for CI and release workflows. The three retired
  compatibility check aliases disappear; all 16 required checks retain their
  current names and branch protection is unchanged.

## 0.5.0 — 2026-09-20

### Added

- Opt-in reference-session explanation workspaces, reserved once at creation
  through `session_config_set_explanation_workspace`, or borrowed through
  `session_config_set_explanation_storage`. TRUE/FALSE share the maximum of
  their profile bounds. No default workspace, growth or allocation fallback;
  overlapping live session storage is rejected. Consumer API v1 and backend
  descriptor ABI 3 remain unchanged.
- Existing direct-text explanation calls reuse a one-entry prepared cache when
  a workspace is enabled: one exploration for measure/write/retry, no reference
  engine allocator calls after session creation. Keys use result generation,
  kind, predicate and typed values, not string addresses. Result release clears
  the internal cache; explicit prepared handles retain their existing lease.
- Python-next `ExplanationKind` and the optional `explanations=` parameter on
  `Ruleset.prepare/solve` use the session cache without changing `explain_true`
  or `explain_false`. The default stays the existing prepared path. Python/CFFI
  still allocate conversion objects, output text and Python strings; configured
  calls no longer allocate a per-call CFFI exploration workspace.

### Changed

- Explanation preparation storage exhaustion now returns the appended public
  `STORAGE_TOO_SMALL` status (-14), including configured session creation,
  `prepare_explanation` and `explain_text_in`. Existing status numbers and ABI
  layouts are unchanged, but consumers matching `PAYLOAD_TOO_LARGE` for this
  case or using exhaustive status switches must update. Text-buffer exhaustion
  still returns `PAYLOAD_TOO_LARGE`; bounded document `status=truncated` remains
  a separate successful outcome. `status_name` and callback validation recognize
  the new code. This is an observable error-contract change, not just an addition.

## 0.4.1 — 2026-09-19

Additive release: consumer API v1 and backend ABI 3 are unchanged. It adds a
one-call, allocation-free explanation path, a per-session storage bound for
the reference backend, and an aligned storage declaration.

### Added

- `maelys_datalog_result_explain_text_in` prepares, sizes, writes and releases
  an explanation in caller-owned storage, with no engine allocation on the
  reference path and no surviving handle, including after a write failure.
  Insufficient preparation storage leaves `out_required` unchanged; a short
  text buffer reports its required length excluding NUL and clears its first
  byte when capacity is nonzero. Document `status=truncated` is distinct from
  `PAYLOAD_TOO_LARGE`: increasing the text buffer does not lift search limits.
  Retrying prepares again; retained prepared handles remain the repeated-write path.
- `maelys_datalog_session_explanation_storage_bound` reports a per-kind upper
  bound including handle/alignment overhead for every reference result of the
  loaded profile. Non-reference backends return `UNSUPPORTED`; backend ABI 3
  and consumer API v1 are unchanged.
- `MAELYS_DATALOG_EXPLANATION_STORAGE(name, bytes)` declares max-aligned byte
  storage in C11/C++17 with a positive integer constant capacity, never a VLA.
  Static application budgets must be checked against the runtime session bound;
  no compile-time SDK bound or hidden allocation is provided.

## 0.4.0 — 2026-09-18

Two public contracts change in this release, both listed under Changed below:
the backend ABI moves to 3, and Why-false text now starts with the shared
`MAELYS-DATALOG-v2` envelope instead of `MAELYS-DATALOG-WHY-FALSE-v1`. The
consumer C API stays at version 1 and only grows.

### Tooling

- Python-next's 28-test suite runs after each matching legacy binding build in
  the Linux/macOS SDK jobs for SMALL and LARGE; required parity cannot skip.
- Manual-only Ubuntu revision-comparison workflow: sequential SMALL/LARGE O2
  builds, two A/A pairs before A B A B, solver and input-index probes, per-metric
  noise floors, explicit indeterminate results and raw run artifacts. This adds
  no PR check and does not change runtime behavior.

### Performance

- Input EDB string lookup uses a preallocated index and bounded undo journal.
  Rejected batches restore the complete arena, including colliding hash chains.
  The storage-requirements query includes index and journal memory separately
  from the text budget.
- Input index entries use one 16-bit disjoint-range encoding for committed
  offsets and pending ordinals, with a 16-bit before-image journal. Index/journal
  sizing is bounded by both fact and text capacities, including the one-byte
  empty string, rather than fact capacity alone. Default index/journal/generation
  storage is 58 KiB SMALL / 116 KiB LARGE, below the original 84 / 168 KiB.
- Input clear advances an 8-bit generation rather than zeroing the index;
  generation wrap alone resets its byte-per-slot generation table. Rejected
  batches preserve even stale index bytes after clear.
- A capacity-only threshold of 16 possible distinct strings selects linear
  lookup without an index/journal for tiny text budgets and indexed lookup
  otherwise. Both regimes preserve atomic append and zero allocation.

### Added

- Complete the C/C++ predicate initializer family with `EDB_QUERY`,
  `POLICY_FACT` and `POLICY_FACT_QUERY` (all prefixed `MAELYS_DATALOG_`),
  mirrored by Python-next `Predicate.edb_query()`, `policy_fact()` and
  `policy_fact_query()`. Query permission never replaces the explicit origin.
  No new native symbol, allocation, layout or ABI change.
- C11 `MAELYS_DATALOG_FACT` and `MAELYS_DATALOG_ADD_FACTS` batch conveniences:
  checked term conversions, exactly-once argument evaluation, automatic
  temporary storage and one atomic native batch call. Range diagnostics
  identify zero-based fact/term positions; no new allocation or ABI change.
- C/C++ `MAELYS_DATALOG_SYMBOL(value)` borrowed symbol initializer and C11
  `MAELYS_DATALOG_QUERY(result, present, predicate, ...)` checked query shortcut.
  The typed API remains available. Query errors leave the output unchanged;
  successful absence writes zero. No ABI change or additional allocation.
- `MAELYS_DATALOG_EDB`, `MAELYS_DATALOG_IDB` and
  `MAELYS_DATALOG_IDB_QUERY` declaration initializers in `<maelys/datalog.h>`.
  They work in C and C++, preserve the existing flags and registration checks,
  and introduce no allocation, exported symbol or ABI change.
- C11 `MAELYS_DATALOG_ADD_FACT(edb, diagnostic, predicate, ...)` convenience
  macro in `<maelys/datalog.h>`: zero to four inferred string/integer/boolean
  terms, checked signed-64-bit range, single evaluation of each argument and
  no extra allocations. `MAELYS_DATALOG_BOOL(value)` explicitly selects boolean
  semantics for C11 integer expressions such as `true` and comparisons.
  Unsupported term types fail compilation; C++ keeps the ordinary typed API.
  Public API version, ABI, exported symbols and structure layouts are unchanged.
- Python Next `Predicate.edb(name, arity)`, `Predicate.idb(name, arity)` and
  `Predicate.idb_query(name, arity)` constructors mirror the C declaration roles.
  They preserve the general constructor, public flags, immutability and existing
  domain-registration validation; no native ABI or error-contract change.
- Python Next explanation conveniences prepare native evidence once in aligned
  CFFI-owned storage, then read its size, write text and release the result lease
  even on output failure. Existing Python signatures and text formats remain
  unchanged; building requires the matching prepared-explanation facade.
- Caller-owned opaque prepared explanations: query aligned storage requirements,
  prepare Why-true/Why-false once, get the cached text size, render repeatedly,
  then release the result lease. The reference path includes Why-false scratch
  and makes no engine allocator calls; prior status, limit and payload semantics
  remain. The independent Why-false envelope migration is listed below.
- Opaque owned `maelys_datalog_input_edb_t` with atomic copied single/batch
  additions, entry count, clear/free and `maelys_datalog_session_solve_edb()`.
  The existing array solve and legacy core EDB API remain unchanged. Input
  buffers retain no borrowed strings and solve results retain no buffer pointer.
- Opaque `maelys_datalog_session_config_t` with checked setters/getters and
  `maelys_datalog_session_create_configured()` in `<maelys/datalog.h>`.
  Sessions snapshot configuration values. Capability constants and the execution
  fingerprint declaration now live in that consumer header; extension headers
  still expose them transitively. Backend descriptors and `session_create_ex()`
  use the separate backend ABI. Python Next no longer includes backend/IR headers
  or depends on the backend ABI. This is additive to consumer API v1.
- Additive opaque C facade getters for loaded-library capacities
  (`maelys_datalog_limit_get`) and all distinct derived IDB facts
  (`maelys_datalog_result_derived_fact_count`). Existing public struct layouts
  are unchanged; limits are append-only scalar keys.
- Experimental `bindings/python-next`, importing as `maelys_datalog_next`,
  compiles CFFI directly against the opaque facade. It buffers `add_fact()` and
  atomic `add_facts()` additions for one native batch, exposes immutable
  `Engine.limits` and `SolveResult.derived_fact_count()`, and keeps the existing
  Python binding separate. It is not a published replacement package.
- Python Next now exposes manifest loading with policy-local vocabulary opt-in,
  policy selection, reusable prepared sessions, policy and execution fingerprints,
  required capabilities and work-budget requests (explicitly unsupported by
  the current reference backend), Why-true/Why-false text, full native
  diagnostics, and result-owned raw terms. Engine handles enforce creating-thread
  use. Prepared sessions still solve complete batches, not incremental updates.

### Fixed

- Python-next bounds iterable staging by the EDB's configured fact capacity and
  warns about unclosed input buffers, sessions and results without performing
  native cleanup during garbage collection.
- Result release frees owned solver results without clearing them first, and
  resets only reusable metadata instead of the entire fact/provenance storage.
  New proof witnesses invalidate reused slots before any truncation path;
  allocator-guard tests also bound release-time bulk reset bytes.
- Opaque solve input diagnostics identify zero-based fact/term indices and the
  cause of predicate, arity, value and capacity failures instead of discarding
  the details as `invalid solve input`. Rejected native batches remain atomic
  and sessions can retry with corrected inputs.

### Changed

- **Text serialization break:** Why-false now starts with `MAELYS-DATALOG-v2`
  and `document=why-false`, replacing `MAELYS-DATALOG-WHY-FALSE-v1`.
  Consumers must dispatch using the document discriminator, not just the version
  line. All following bytes and statuses are unchanged; Why-true output is
  byte-identical. No source-language, C ABI, backend identity, policy/program/
  execution fingerprint or query-result change. Python-next returns the new
  native document unchanged. No legacy-output mode is provided.
- **Backend ABI 3:** direct `explain_true`/`explain_false` callbacks are replaced
  by `explanation_storage_requirements`, `explanation_prepare` and
  `explanation_write_text`. An explanation-capable backend must implement all
  three without allocation; ABI 1/2 descriptors and options are rejected, not
  reinterpreted. Rebuild providers against the new header and migrate callbacks.
  The existing consumer `result_explain_*_text` functions remain available as
  allocating convenience wrappers over the same preparation path. This ABI
  change leaves the consumer API version, language version and text formats
  unchanged; the separate Why-false envelope migration is described above. The
  reference name/semantic ID remain `reference` / `maelys.reference.v1`; the
  backend ABI number is not an execution-fingerprint input, so this ABI change
  does not change reference execution fingerprints at fixed program/options/
  profile. A third-party backend changing its semantic ID changes its fingerprint.
- Input EDB storage is now fixed-capacity: `storage_requirements`/`init` support
  caller-owned aligned storage without allocation; `create_with_capacity` uses
  one allocation and `create` reserves profile-bounded capacity. Append,
  batch append, count and clear never allocate or grow storage. Text capacity
  includes distinct names, symbols and their NUL terminators. Failed batches
  consume neither entries nor bytes. Python Next accepts optional
  `fact_capacity`/`text_capacity` budgets. The default text budget is now the
  native symbol pool plus registry names (40 KiB, not 5/10 MiB); repeated strings
  share storage. `INPUT_EDB_TEXT_BYTES` reports this bound through `limit_get`.
- Opaque sessions reuse bounded scratch storage for input conversion and
  external-backend canonical export. Reference/prepared sessions now reserve
  native and public results at initialization: solving and result release no
  longer allocate or free. Provenance stays preallocated; requested explanations
  can still allocate bounded workspaces. The one-live-result lease is unchanged.
  Hot-path libc qsort calls are replaced by a typed in-place introsort: ordered
  input fast path, logarithmically bounded stack and worst-case heapsort fallback.
  A whole-engine allocator-guard test disables allocation through repeated solves,
  queries, failed transactions and result release. Custom backends/callbacks,
  Python/CFFI allocations and libc internals are outside this guarantee.
- Python Next `Edb.add_fact()` / `add_facts()` now store inputs in the native
  opaque EDB instead of a persistent Python list. Total entry and individual
  string limits fail at insertion; policy-domain, declared-arity, symbol-pool
  and per-predicate limits remain solve-time checks. Batches stay atomic,
  successful solves still freeze Python EDB mutation, and existing calls remain
  valid. `len(edb)` and pre-solve `edb.clear()` expose native buffer operations.
  Explicit `edb.reset()` starts a new batch in the same storage after a successful
  solve without invalidating a live result or releasing its session lease.
- `scripts/publish-channel.sh` honours `CHANNEL_DRY_RUN=1`, set by
  `maelys-release rehearse --channel` (socle 0.42.0): it takes its real path
  up to the registry's write — assembly, the tarball as a file, the registry
  read — instead of returning early on a version the registry holds, and
  stops there. Its record for the channel marker drops `already_published`,
  which differed between a publication and its rehearsal by construction.
  The socle is pinned at v0.47.0, whose 0.46.1 makes the marker carry that
  record.

## 0.3.1 — 2026-09-12

### Fixed

- The npm channel of 0.3.0 never published: `scripts/publish-channel.sh`
  handed npm the tarball as `dist/maelys-dev-datalog-wasm-0.3.0.tgz`, which
  npm reads as the GitHub shorthand `owner/repo` and tries to clone over
  SSH. The path now starts with `./`. The GitHub release of 0.3.0 is
  complete and untouched; the channel job runs the script of the tag it
  publishes, so the remedy is this release, not a replay.

## 0.3.0 — 2026-09-11

### Changed

- Releases go through the `maelys-release` socle (v0.35.0): `release.yml` is
  rendered from `maelys-release.conf` (three native targets plus `wasm32`, the
  receipts under the manifest, the npm channel, the reviewer gate, the
  version header regenerated by `[cut] after-version`), the cut is
  `maelys-release cut` after the second-compiler gate of
  `scripts/release-gates.sh`, and the npm channel is
  `scripts/publish-channel.sh`, idempotent on a replayed tag.
  `package-release.sh` builds one target per run — `wasm32` installs the
  pinned emsdk itself — and writes one immutable
  `release-receipt-<target>.json` per target; what a channel published is
  recorded by the socle in `channel-<name>.json`, never in a receipt.
  `cut-release.sh`, the receipt merge and `--record-channel` are gone. This
  is the first release the socle's workflows publish.
- The MAELYS-DATALOG-v2 specification is licensed on its own terms: the prose
  under CC-BY-4.0, so it can be quoted, translated and derived from; the ABNF
  grammars and the `.dl` corpus beside it under MIT, because an implementer
  copies them into a parser or a test suite. The engine stays MPL-2.0.
  `LICENSING.md` states each part of the repository and names every
  document it engages publicly, `docs/validation.md` and
  `docs/release-engineering.md` included; the stale
  `docs/extension-sdk-validation.md` is removed.
- The repository follows the shared `maelys-release` conventions: managed
  instruction blocks in `AGENTS.md` and `CLAUDE.md` (CC-BY-4.0, from the
  socle), `SECURITY.md`, and `maelys-release check` verifying them in the
  shared CI job of `ci.yml`.

## 0.2.0 — 2026-09-10

### Added

- npm channel on GitHub Packages: `@maelys-dev/datalog-wasm`, assembled from the
  release's own attested WASM tarballs and published by the tagged workflow with
  the run's `GITHUB_TOKEN`. The npmjs.com channel never published: trusted
  publishing cannot create a package, so every tagged run since `0.1.0-alpha.2`
  ended on a 404.
- Four MIT-licensed, copyable extension starters, separate from the MPL working
  examples. Installed-SDK smoke tests cover registration and explicit rejection
  of unimplemented callbacks; engine and existing SDK licenses are unchanged.
- Composite SDK example: a `permit` frontend and its `exact_match` filter in one
  declaration, with end-to-end solving/proofs, explicit selection, dependency
  rejection, source locations and atomic-registration checks in native/WASM CI.
- Common extension declaration ABI v1 and opaque native contexts with atomic
  registration, immutable catalogues, named frontend/backend/planner selection
  and context-local filters. Existing global and typed APIs remain available;
  domains stay process-wide and manifest/binding selectors are unchanged.
- Four uniform standalone extension projects and an installed, test-only
  conformance kit, exercised against static/shared SDKs in both size profiles.
- Concurrent context isolation, retained-catalogue lifetime, registration
  rollback and compatibility regression coverage. Internal ruleset POD consumers
  must rebuild; public typed extension ABIs are unchanged.

### Changed

- Releases carry no `-alpha` suffix: the `0.` already states that the API may
  break between minor versions. `cut-release.sh` accepts `X.Y.Z` only, its
  CHANGELOG gate reads `## X.Y.Z — <date>`, and the npm dist-tag follows the
  series (`next` while `0.x`). This aligns the repository on the shared
  `maelys-release` conventions; the release mechanism stays its own.
- Directories are named after what they ship: `src/registry/` for the module
  registry, `modules/standard/` for the filters the engine ships through the
  SDK, `sdk/examples/` for third-party examples, `tests/fixtures/` for shared
  test material.
- The release receipt records a channel only after that channel published.
  `0.1.0-alpha.4` shipped a receipt asserting an npm package whose publication
  had failed, and that receipt feeds the public version line.
- A broken third-party apt source of the runner image no longer fails CI: the
  update reports, the install decides, and a missing package still fails loudly.
- WASM C boundary and JavaScript wrapper are grouped under `bindings/wasm/`;
  distributed filenames, exported functions and wrapper APIs are unchanged.
- Extension examples now live under `sdk/examples/`, alongside the conformance
  kit. Root `examples/` contains engine-usage sources only; its executable and
  macOS debug bundle are generated under `build/examples/`.

### Fixed

- The release published as `0.1.0-alpha.4` is marked as a prerelease, and its
  receipt no longer names an npm package that does not exist.
- Python validation replaces native libraries through fresh files when changing
  size profiles, avoiding macOS code-signature page-cache kills after rebuilds.
- WASM allocation tests resolve lazy Emscripten exports before installing
  instrumentation, preserving failure injection and exact allocation counts
  with the release toolchain. CI covers Emscripten 3.1.61 and 4.0.14 in both
  size profiles; engine and binding behavior are unchanged.

## [0.1.0-alpha.4] - 2026-09-06

### Added

- Backend ABI v2: optional EXPLAIN_FALSE capability and read-only Why-false text
  API, backed by the existing bounded reference diagnostic extractor. The naive
  backend returns UNSUPPORTED. ABI v1 backend descriptors must be rebuilt.
- Dedicated public MALFORMED_PROGRAM diagnostic for structurally invalid IR.
- Public validated program IR, explicit per-load frontends, source locations and
  per-session solver backends with capability negotiation and atomic failure.
- Independent public-only arrow-language and naive positive-Datalog examples,
  including recursive differential tests and typed IR round-trip validation.
- Separate compiled-program and execution fingerprints covering domain/schema,
  query restrictions, frontend/backend semantic identities and execution options.
- Versioned public C SDK for separately compiled string-filter and join-planner
  modules, with bounded startup registration and immutable module identities.
- Installed-SDK consumer tests, static/shared integration tests, callback-error
  and budget checks, and cross-process semantic fingerprint tests.
- Validation matrix (`docs/validation.md`): every native test in both size
  profiles under ASan/UBSan, out-of-tree installed-SDK consumers with C11/C++17
  headers and opaque-handle rejection, the real Python and Node/WASM wrappers in
  both profiles, and bounded fuzz smokes, all wired into CI.

### Changed

- Standard inline compilation now uses the generic frontend pipeline, with one
  common validation pass and a compiled-program fingerprint cached at finalization.
  The reference backend reuses the runtime's prepared session and materialized
  inputs; canonical public facts are only produced for external backends. Legacy
  identity/proof transcripts have SMALL/LARGE regression goldens; grammar, solver
  algorithm and language bindings are unchanged.
- Diagnostics keep their historic order: when a later clause fails to parse, an
  earlier clause-local error (unsafe variable, base predicate in a rule head,
  rejected filter pattern) is still the one reported, and stratification is
  still checked after the last clause. A pattern rejected by a filter module is
  now located at the end of its clause rather than after the pattern token.
- Policy identity follows the frontend descriptor the host selected: only the
  built-in descriptor keeps the source-hash authority; a copied descriptor that
  wraps the standard lowering carries the extended identity, as before.
- The opaque native C session API dispatches through the reference adapter by
  default. Existing source authority fingerprints, results and proof formatting
  are preserved; legacy/Python/WASM entrypoints remain on the reference engine.
- Binding safety, structural checks and stratification are shared by all
  frontends. No parser hooks or mutable grammar registry are introduced.
- Standard string filters now live in `modules/standard/` and use the same SDK
  as external modules; the default behavior and standard fingerprints remain.
- Extended policies bind filter/planner semantics into their executable identity
  after source integrity verification. Module failures remain fail-closed.
- All build variants use shared source manifests. Public CMake include paths no
  longer expose private engine headers; native packages include the module SDK.

- The `MAELYS-DATALOG-WHY-FALSE-v1` text is part of the public explain-false
  contract: limit hits are named (`none`, `candidate-rules`, `substitutions`,
  `depth`, `diagnostics`, `filter-cost`) instead of a raw bitmask, and `?N` /
  `binding=N` are documented as rule-local IR variable ids. The reference's
  bounds are fixed in backend ABI v2.

### Fixed

- Native and npm packages preserve the vendored yyjson license notice.

## [0.1.0-alpha.3] - 2026-09-03

### Fixed

- The WebAssembly build links `maelys_datalog_filter.c`, which the native
  builds already compiled; the release workflow of `v0.1.0-alpha.2` failed
  on its undefined filter symbols, so that tag has no release.

## [0.1.0-alpha.2] - 2026-09-03

### Changed

- Relicense from MIT to the Mozilla Public License 2.0, the license of every
  Maelys repository. The vendored `yyjson` keeps its MIT license. No code
  change.

## [0.1.0-alpha.1] - 2026-07-30

### Added

- First public alpha release of the bounded deterministic Datalog engine.
- Native C11 API and Python, JavaScript, WebAssembly, and WASI bindings.
- Semi-naive fixed-point evaluation with stratified negation.
- Bounded `SMALL` and `LARGE` memory profiles.
- Policy identity, diagnostics, proof records, and decision receipts.
- Public documentation, contribution guide, security policy, and CI workflow.

[Unreleased]: https://github.com/maelys-dev/maelys-datalog/compare/v0.2.0...HEAD
[0.1.0-alpha.4]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.4
[0.1.0-alpha.3]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.3
[0.1.0-alpha.2]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.2
[0.1.0-alpha.1]: https://github.com/maelys-dev/maelys-datalog/releases/tag/v0.1.0-alpha.1
