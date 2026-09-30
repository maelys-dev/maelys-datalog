# Changelog

All notable changes to Maelys Datalog are documented in this file.

The project follows [Semantic Versioning](https://semver.org/) and uses the
format described by [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- Extend loaded-library limit queries with policy atom count and byte bounds.
  Add allocation-free scalar queries for a selected policy's compiled predicate,
  fact and normalized rule counts, with Python and common JavaScript/TypeScript
  accessors. Build ceilings, program counts and effective session quotas remain
  separate. No solver, fingerprint or backend ABI change.
- Add explicit fixed-vocabulary retained session inputs in
  `maelys/datalog_transactions.h`: caller-owned storage, independently bounded
  raw add/remove batches, add-wins linear composition, snapshot replacement,
  incarnation/generation validation and atomic publication. ABI 5/6 providers
  continue receiving complete snapshots. Existing window adapters use each
  bank's replacement entry; result/explanation leases still govern publication.
  Ordinary sessions keep their behavior and identity; the opt-in contract has
  a distinct execution fingerprint. No incremental derivation algorithm or
  growing input dictionary is introduced.

## 0.15.0 — 2026-09-30

The common JavaScript/TypeScript binding replaces the retired WASM package.
The optional backend allocator and input-delta API remain separate proposals.
Consumer API 2, program ABI 2 and backend ABI 5/6 are unchanged.
Python performance review and release approval are recorded in the changelog
pull request before the release cut.

### Added

- Add one JavaScript/TypeScript consumer API with native Node-API and WASM
  implementations, independent resource ownership, manifests, fixed capacities,
  typed results, diagnostics and canonical explanations. Package both profiles
  as `@maelys-dev/datalog`. This release retires `@maelys-dev/datalog-wasm`,
  `MaelysPlayground`, the old C bridge, build scripts and legacy archive/channel
  immediately; pinned historical releases remain available. See the
  [migration contract](bindings/javascript/README.md#sdk-compatibility-and-migration).
- Add `maelys_datalog_policy_load_manifest_buffer` with the same manifest
  contract as the stable file loader; preserve Advanced memory loading semantics.
- Pin native JavaScript compatibility to glibc >= 2.34 or macOS arm64 >= 13.5,
  with stripped addons exporting only Node-API registration. Release packaging
  uses a checksummed Node distribution and rejects binary compatibility drift.
  Python parity constructs its own clean SDK; Node CI includes 22, 24 and 26.

### Changed

- Adopt maelys-release v0.62.2 through its generator, pinning commit
  `5148671abf3a1b437025881d10fd768efb9de5b3` for CI, release and registry channel
  workflows. Artifact construction and the product runtime are unchanged by
  this adoption.

## 0.14.0 — 2026-09-29

Performance tradeoff accepted by David on 2026-09-29; publication follows the
normal release checks.
Consumer API 2 and program ABI 2 remain unchanged. Backend ABI 6 is additive;
ABI 5 remains available under its existing default resource contract.

### Added

- Fixed session capacities E/D/S/T and a separate backend ABI 6 resource
  contract. E/D payload reservations are sized; S/T are exact admission limits,
  but lowering S/T does not yet reduce the profile-sized dictionary reservation.
  Quotas are normalized separately from program/build bounds and passed to both
  ABI 6 sizing and preparation. Unsupported nondefault ABI 5 combinations and
  reserved elastic/allocator modes are refused explicitly. Default behavior
  and execution identity are preserved.

### Changed

- Native session constructors consolidate their host regions into one aligned
  allocation and can reuse one bounded equal-sized idle block after close.
  Caller-owned storage and active result leases are excluded. Retained memory
  is additional to live-session reservations and is released at native library
  teardown; no malloc tuning or public API is added. Reuse is conditional on
  size and availability; Python/CFFI still allocate. See the
  [ownership and qualification limits](docs/validation/session-recycling.md).

- Disposable `solve_once` workspaces use `malloc` plus metadata initialization
  instead of zeroing the complete reservation with `calloc`. Used facts, proof
  indices and provenance validity are initialized before reading; unused payload
  is not exposed. This removes a fixed initialization cost from the allocating
  microbenchmark, not from an already prepared session. See the scoped local
  and hosted timing evidence and qualification limits in
  [session resources validation](docs/validation/session-resources.md).

### Performance review and accepted limits

- Merged main `9260b2ded45cc0a4595c4ad1a444b638f46005ed` passes all 17 CI checks.
  The complete default-glibc [Python run 36610272911](https://github.com/maelys-dev/maelys-datalog/actions/runs/36610272911)
  has valid positive controls in all 32 case/configuration pairs, but remains
  `review_required`. Compared with v0.13.0, SMALL/default prepared requests
  retain three complete-request alerts beyond the matching null envelope:
  seven-symbol medians +1.78% / +2.18% in the two rounds, and 93-integer median
  +2.06% and p95 +2.56% in round two (round one indeterminate). These observations
  are not attributed to an engine mechanism. The report also retains 39 warm
  phase triggers and informative cold findings.
- In that same run, convenience-request medians are lower beyond their A/A
  floors in both rounds by 10.45–16.02% for SMALL/7 facts, 57.73–62.96% for
  LARGE/7 and 15.20–21.49% for LARGE/93. These comparisons include multiple
  changes and do not attribute the whole improvement to recycling. They do
  not cancel the prepared-session observations.
- The [named release review](docs/validation/v0.14-release-review.md) records
  immutable revisions, report SHA-256, raw verification, controls, all remaining
  findings and attribution limits. Earlier reports, including 93-integer
  prepared +1.84–4.29% and the isolated seven-symbol +21.75% median event,
  remain preserved in [session recycling validation](docs/validation/session-recycling.md).
  David explicitly accepted this named report's documented tradeoff for 0.14.0
  publication on 2026-09-29, including these unresolved observations. The
  decision is separate from earlier merge-only approvals and does not claim
  absence of regressions or an explanation of the remaining latency events.

## 0.13.0 — 2026-09-27

Transactional window context and explicit expiration. Consumer API 2, program
ABI 2 and backend ABI 5 are unchanged; the retired header path is an intentional
source migration.

### Changed

- Both window adapters are declared in `<maelys/datalog_window.h>`.
  `<maelys/datalog_group_window.h>` is removed from the source and installed SDK,
  without a forwarding header. Replace that include and rebuild consumers
  against a clean SDK prefix; an in-place installation can leave obsolete files.
  Existing handles, signatures and public record layouts are retained.

### Added

- Configured window storage/init functions accept a bounded static-fact capacity
  in `maelys_datalog_window_options_t`. `replace_static` replaces the complete
  mutable static EDB and recomputes it with retained events immediately, without
  inserting an event or consuming an occurrence/group ID. NULL/0 clears it.
  Static input survives FIFO eviction and participates in normal set semantics;
  group facts survive while any retained group or static input supplies them.
- Opt-in expiration storage supports `push_until` with an absolute caller-defined
  uint64 deadline and `expire(now)` without insertion. Expiry removes every timed
  event/group whose deadline is <= now, retains survivor order and static input,
  and consumes no ID. The watermark advances only on success and cannot move
  backwards; new deadlines at/before it are rejected. Ordinary pushes remain
  untimed. No clock is read and no insertion implicitly advances time.
- An expiry with nothing due updates only the watermark: no solve, backend call
  or result invalidation occurs. Other mutations publish only after candidate
  solving and release of the old result lease. Failures preserve committed facts,
  text, result, cursor and watermark, including live explanation leases, backend
  errors and derivation/aggregate failures after removal through negation.
- Caller-owned storage queries include both candidate/committed banks and all
  adapter scratch. Static and event slots have separate raw bounds and share a
  per-bank text budget; no runtime allocation, growth or heap fallback is added.
  Deadline arrays are reserved only when enabled. Their payload on the checked
  64-bit layout is 32 bytes per event/group slot across both banks, plus queried
  alignment padding. Sessions, results and explanation storage remain additional.
- A session-capacity design and reproducible native storage inventory document
  the current 286,040/476,504-byte reference reservation and explicitly separate
  arithmetic payload projections from implemented storage requirements. No sized
  session API is introduced. Program/build limits exposed through `program_info`
  remain distinct from proposed per-session quotas.

### Validation

- Both adapters are checked against independent snapshots for static replacement,
  revocation without insertion, canonical IDs, duplicates, normalization, FIFO
  retention and rejection. Expiry checks cover unordered/equal/maximal deadlines,
  mixed untimed events, empty and multi-fact groups, survivor slices, exhausted
  IDs, no-op result stability and rollback after negation-induced overflow.
- Allocation guards include input and deadline banks in byte-exact rollback
  comparisons and exercise success/reuse with engine allocators disabled.
  Installed static/shared SDK consumers check exact canonical inputs and the
  backend ABI 5 commit/abort boundary. SMALL/LARGE, memory scribbling and
  ASan/UBSan checks pass. These are functional and allocation guarantees, not
  a claim of faster solving or whole-application zero allocation.

### Python performance and measurement limits

- The [integrated candidate report](https://github.com/maelys-dev/maelys-datalog/actions/runs/36331650860)
  on `21ca681` validates the injected three-request control in both rounds of all
  32 case/configuration pairs. It remains `review_required`, not a general
  absence of regressions. Against v0.12.0, SMALL-default / 7-integer-prepared
  complete-request median is +2.14% / +0.68% (A/A floor 0.58%), and LARGE-default /
  7-symbol-prepared cold median is +2.80% / +6.28% (floor 1.82%). Both classify
  slower in both rounds. The warm medians are 82.874/83.045 microseconds versus
  81.141/82.484; the cold medians are 249.375/248.313 versus 242.592/233.645.
- Against the durable v0.11.1 anchor, 27 of 32 warm complete-request medians
  classify faster in both rounds, but LARGE-Release / 7-symbol-prepared cold
  p95 is +22.29% / +6.67% (floor 4.55%). Single-round alerts remain visible,
  including LARGE-Release / 93-integer-prepared complete-request p95 at
  +26.00% / +1.14% against v0.12.0 (floor 4.56%; slower/indeterminate).
- The artifact retains every phase observation and separate observer/storage
  control. These controls also show timing perturbations; they do not correct
  the candidate measurements or explain their cause. Separate phase and total
  loops cannot be paired by sample index. A/A floors describe within-binary
  repeatability, not all placement or runner effects. No engine, binding or
  runner cause is established by these timings; the concrete release decision
  is recorded in the changelog pull request.

## 0.12.0 — 2026-09-27

Session storage reductions and Python consumer performance review. Consumer
API 2 and backend ABI 5 are unchanged.

### Changed

- Reserve canonical input export storage only for sessions whose backend solve
  callback consumes exported facts. The reference borrows materialized inputs
  directly and no longer reserves the unused export array. External backends
  retain their canonical input contract and creation-time reservation; no
  additional allocation or public API/backend ABI change.
- Reference results expose a read-only view of the finalized native derived
  facts, eliminating the public result's duplicate array and its export/import
  conversions. External backends keep runtime-owned emission storage reserved
  at session creation. Canonical enumeration and result/explanation leases are
  preserved.
- Prepared native results borrow session-owned materialized inputs under the
  same result/explanation lease. Legacy direct solves and copying workspaces
  retain independent EDB snapshots. Caller input buffers may still be cleared
  after solve. No allocation is deferred to solve or explanation preparation.
- Normal transaction resets copy only live policy symbol text and entries plus
  the full hash index, avoiding the unused dictionary capacity. Rejections keep
  the full byte-for-byte dictionary restoration. Session reservation is unchanged
  by this reset optimization.
- On the checked 64-bit SMALL/LARGE builds, a reference session sharing an
  engine-owned policy reserves 286,040/476,504 bytes instead of 515,416/935,256,
  saving 224/448 KiB across the same three engine allocations. Policy creation
  and optional workspaces are excluded. This is a reservation reduction, not
  a claim of faster solving or lower Python process RSS.

### Validation

- Python lifecycle performance now has deterministic call budgets in SDK CI and
  an automatic main-commit benchmark against the previous release and v0.11.1,
  with v0.11.0 retained as an informative historical comparison. A benchmark-only
  positive control executes three complete requests using the same v0.11.1
  binary; detection is required in both rounds of every case/configuration.
  This validates detection of that coarse cost, not arbitrary smaller slowdowns.
  Before cutting a release,
  maintainers review the report and record their decision in the changelog PR.
  Timing observations remain visible without an automatic timing gate at the
  tag; they are not automatically attributed to the engine. See
  `docs/python-performance.md`.
- Schema-3 Python measurements record contemporaneous wall/thread CPU clocks,
  CPU IDs, resource counters and GC intervals, with fixed sample storage and
  adjacent calibration probes. Separate observer/storage controls retain their
  perturbation. Original uninstrumented reports remain unchanged; the two
  protocols' absolute latencies are not interchangeable.

### Performance and measurement limits

- The [hosted Python measurement](https://github.com/maelys-dev/maelys-datalog/actions/runs/36308020794)
  on integrated commit `f975be6` detected the injected control in both rounds of
  all 32 case/configuration pairs. Against v0.11.1, 30 of 32 warm complete-request
  medians and all eight cold medians classify faster in both rounds; the other
  warm medians retain indeterminate rounds. This validates detection of the
  injected three-request cost, not sensitivity to every smaller regression.
- The report remains `review_required`: SMALL-Release / 7-integer-prepared
  complete-request p95 is 221.984 versus 89.587 microseconds in one round
  (+147.79%, A/A floor 4.63%), and 90.980 versus 88.955 in the other (+2.28%,
  indeterminate). The earlier run also retained a single-round p95 alert for
  this case. No complete-request metric is slower in both rounds of the new
  run, but the recurring tail observation remains unresolved. Phase diagnostics
  additionally retain SMALL-Release / 93-symbol-prepared query p95
  (+10.92% / +10.27%) and SMALL-default / 7-symbol-prepared input median
  (+0.89% / +0.82%) above their own floors in both rounds. Timings do not
  establish an engine, allocator or runner cause, or a general latency guarantee.
- The [bounded instruction diagnostic](https://github.com/maelys-dev/maelys-datalog/actions/runs/36313532322)
  reuses the original SDK binaries and checks 16 declared transaction positions
  in 7/93-integer-prepared requests. All 128 profiles and repeated per-function
  counts validate; complete-request Callgrind Ir decreases by 7.67--7.94%, and
  native function counts stay constant across the positions within each case
  and revision. These separate software counts neither measure hardware cycles
  nor establish the cause of the original transient latency event.
- The [instrumented full matrix](https://github.com/maelys-dev/maelys-datalog/actions/runs/36316284376)
  on `b0c312b`, with unchanged runtime/binding code, validates the injected control
  in all 32 case/configuration pairs and retains 814,464 request observations.
  No warm total median or p95 exceeds its A/A floor in that run. It still reports
  `review_required`: SMALL-default / 7-symbol-solve cold p95 is -1.48% / +4.65%
  (floor 1.55%); phase alerts include LARGE-Release / 7-symbol-solve query p95
  +2.92% / +3.92% (floor 1.46%). This does not reclassify earlier reports.
- The maintainer accepted these documented uncertainties for 0.12.0 after
  reviewing the reservation gains, functional checks and bounded diagnostics.
  The original +147.79% event remains unattributed; publication is not a claim
  of universally unchanged or improved Python latency. Self-hosted ARM64
  measurements remain a separate follow-up when a runner is available.

## 0.11.1 — 2026-09-26

### Changed

- Sessions share engine-owned immutable compiled policies and keep a separate
  transaction dictionary, eliminating full ruleset copies on that path.
  Caller-owned policy storage still receives an independent session snapshot.
  Closing a policy releases its handle; shared storage survives until the last
  session closes. No session cache, public API or backend ABI change.
- Session destruction no longer clears storage immediately before freeing it.
  Reserved public/native results initialize metadata only, using the same payload
  validity rules for their first solve and subsequent reuse.
  Default Python preparation uses the public session constructor without an
  intermediate configuration handle. Explicit configuration remains supported.
- Native result arrays retain their pre-change offsets; the transaction
  dictionary pointer is stored after the payload and assigned before each solve.

### Performance and measurement limits

- A reference SMALL session sharing an engine-owned policy reserves 515,416
  bytes instead of 1,178,352 (56.3% less), across the same three engine
  allocations. This excludes the already-created policy and optional
  explanation/backend storage; Python/CFFI still allocate their own objects.
- On the original candidate before the final result-field relocation, a local
  SMALL quickstart with Python 3.14.7 on macOS arm64 measured a total median of
  39.708 instead of 68.958 microseconds versus 0.11.0 (42.4% less). It remained
  2.03% above the 0.9.1 reference, above that reference's 0.43% A/A floor.
  Local Linux arm64 Callgrind counts for SMALL/7 session creation fell from
  176,058 to 28,370 software instructions. These setup measurements were not
  repeated after the field relocation and are not general latency guarantees.
- For the final engine layout, eight declared prepared-session fixtures on
  hosted x86-64 retain an increase of 0.217% to 0.722% in scoped Callgrind Ir
  versus 0.11.0. Per-function Ir/Dr/Dw repeat across processes and the two text
  placements; materialization and string comparisons contribute to the extra
  work. These are software counts, not hardware cycles or a bound for every
  program. The [three-revision run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36265486587)
  retains the measurements and checked outputs.
- The same run's complete session matrix retains 336 slower, 30 faster and
  256 indeterminate metrics for revised/base; revised/original retains
  146/27/449 respectively. Restoring the offsets did not remove the observed
  regressions. Their attribution remains unresolved by this protocol: A/A
  floors describe one binary's repeatability, and equal instruction counts on
  the eight diagnostic fixtures establish neither equal cycle costs nor
  equivalence of the full matrix. The setup and memory gains are an explicit
  tradeoff with the quantified instruction cost and unresolved latency effects,
  not a claim of regression-free prepared sessions. The
  [earlier hosted run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36257327159)
  remains separate evidence from a different host.

## 0.11.0 — 2026-09-25

Backend preparation storage and publication acceptance
([#115](https://github.com/maelys-dev/maelys-datalog/pull/115)).

### Breaking

- Backend ABI 5 adds `storage_requirements`, changes preparation to
  `prepare(program, storage, out_state)`, and requires an infallible `commit`.
  ABI 4 descriptors and mismatched descriptor sizes are rejected before any
  callback. Custom backends must be recompiled and migrated.
- Caller-owned backend storage is configured through the opaque session config;
  the host validates size/alignment and never accesses or frees its contents.
  A successful solve remains a candidate until host acceptance; built-in windows
  commit only at publication, and discard unpublished candidates without commit.
  Consumer API 2, program ABI 2 and diagnostic ABI 1 (1328 bytes) are unchanged.
  See the [0.11.0 migration addendum](docs/api-type-migration.md#0110--backend-abi-5).

## 0.10.0 — 2026-09-24

Coordinated C, Python, Wasm and SDK migration. The released contracts are
consumer API 2, frontend/program ABI 2 and backend ABI 4. Recompile C/CFFI
consumers and migrate removed interfaces; there are no compatibility aliases.

- C declarations, input types and callback/diagnostic contracts change together
  in [#98](https://github.com/maelys-dev/maelys-datalog/pull/98),
  [#102](https://github.com/maelys-dev/maelys-datalog/pull/102) and
  [#103](https://github.com/maelys-dev/maelys-datalog/pull/103).
- Python [#106](https://github.com/maelys-dev/maelys-datalog/pull/106),
  Wasm [#107](https://github.com/maelys-dev/maelys-datalog/pull/107) and native
  archives [#108](https://github.com/maelys-dev/maelys-datalog/pull/108) move
  together to the installed public SDK.
- This release also includes domain initializers
  [#101](https://github.com/maelys-dev/maelys-datalog/pull/101), solver changes
  [#104](https://github.com/maelys-dev/maelys-datalog/pull/104) and
  [#105](https://github.com/maelys-dev/maelys-datalog/pull/105), reproducible
  archives [#109](https://github.com/maelys-dev/maelys-datalog/pull/109), and
  aggregate diagnostics [#110](https://github.com/maelys-dev/maelys-datalog/pull/110)
  with defensive initialization [#111](https://github.com/maelys-dev/maelys-datalog/pull/111).

### Added

- Aggregate rejections now distinguish `solve_aggregate_domain_error` from
  `solve_sum_overflow`, preserving `INVALID_FIELD` and atomic failure. The
  diagnostic names the source predicate, operator, projected argument, offending
  typed value (or first overflowing partial sum) and numeric bound. A new
  `DIAGNOSTIC_AGGREGATE` presence bit uses existing fields; public layouts and
  backend ABI 4 are unchanged. Update exhaustive diagnostic-code switches for
  the two additive codes. Python carries the common fields; Wasm exposes an
  immutable `aggregate` section with exact decimal integer text.

- Advanced operations on the existing application handles: in-memory manifest
  bundles, caller-owned policy storage, bounded domain installers, composed
  session backend/context configuration, reference structured explanations,
  filter statistics and the decision-precedence helper. Caller-owned policy
  storage does not imply a heap-free loader or session constructor.

- `MAELYS_DATALOG_DOMAIN_NO_ATOMS` and `MAELYS_DATALOG_DOMAIN_WITH_ATOMS`
  initialize public domain declarations in C and C++, deriving counts from
  fixed-size arrays. Registration remains explicit; no allocation, exported
  symbol, layout or backend ABI change. An empty policy-source vocabulary does
  not restrict request EDB symbols or change loading permissions.

### Changed

- Initialize aggregate rejection scratch before evaluation, including on the
  successful path. Current rejection sites already populate it; this defensive
  change protects against a future propagated error that omits those fields.
  Aggregate evaluation remains outside the ordinary rule-derivation frame.
  Public layouts, diagnostic codes, statuses and backend ABI 4 are unchanged.

- Native release builds retain debug information with canonical source/build
  paths. Tar/gzip metadata is normalized to `SOURCE_DATE_EPOCH` (default: source
  commit time). Two independent builds under different paths are checked on
  SMALL/LARGE. Reproduction requires the same toolchain, flags and inputs;
  receipts/provenance remain records of each execution.

- Native SDK archives now use the CMake installation inventory. Ship the complete
  public `maelys/` surface, including `datalog_details.h` required by the facade
  and `datalog_advanced.h`, plus the installed conformance kit and MIT starters.
  Stop distributing `maelys_datalog.h`, the historical version-macro header,
  `src/` and `common/` implementation headers; migrate consumers to the public
  facade or advanced/extension headers and recompile for 0.10.0. Native release
  archives remain SMALL/static; CMake still supports SMALL/LARGE and shared
  libraries. Extracted-archive consumers and deliberate missing/private-header
  mutations now guard packaging on both profiles. No engine/API/ABI change.

- Replace the Wasm native-object binding with an installed-public-SDK consumer.
  A single typed API accepts atomic multi-predicate batches and arities 0–4,
  transports signed int64 exactly (`bigint`, or safe integer `number` inputs),
  and returns all integers as `bigint`. Add explicit source atoms, Why-false,
  immutable structured diagnostics, three fingerprints and explicit `close`.
  Remove input symbol IDs, raw engine exports and historical core/examples JS
  targets without aliases. Types are supplied by the SDK and mandatory in the
  release package. Unknown-symbol explanations now throw NOT_FOUND; absent
  Why-true returns canonical not-derived text. See [the v0.10.0 binding guide](https://github.com/maelys-dev/maelys-datalog/blob/v0.10.0/bindings/wasm/README.md)
  for the full migration and allocation/lifetime contracts. Native engine API,
  backend ABI, algorithms and layouts are unchanged by this adapter migration.


- Consolidate Python on one `maelys_datalog` package, using the former
  `python-next` implementation directly against the installed public SDK.
  Remove `maelys_datalog_next`, the native-object C shim, its CMake option/target,
  and ruleset-scoped raw symbol inputs without compatibility aliases. Sessions,
  atomic bounded inputs, result-owned term views, diagnostics and both explanation
  kinds are available through the single package. `Status` names native errors.
  Predicate flags, explanation absence, thread confinement and explicit cleanup
  require caller migration; see `bindings/python/README.md`. Python/CFFI still
  allocate. SMALL/LARGE tests compile outside the checkout against a fresh SDK;
  independent expected answers and migrated V1 contracts replace dual-binding
  parity. Native archives now share CMake's public installation inventory.


- Reference solving classifies base-predicate presence once per rule application
  and skips base fact membership for derived heads whose predicate is absent.
  Low-level hand-built bases retain duplicate suppression before capacity errors,
  with binary membership in the sorted EDB. IDB duplicate handling, storage
  bounds, existing field offsets and public/backend contracts are unchanged.

- Reused sessions reset live metadata and the required pointer/index scratch
  instead of clearing their whole fact and symbol-pointer capacity on every
  successful materialization. Each inserted fact is fully initialized before
  publication; rejected transactions retain complete cleanup. Storage bounds,
  canonical results, allocation guarantees and ABI layouts are unchanged.

- **0.10.0 migration:** the common diagnostic has an explicit size/version
  initialization protocol, separate status and precise code, independent detail
  sections and owned bounded text. Consumer API 2, frontend/program ABI 2 and
  backend ABI 4 reject older callback contracts before invocation. The private
  solver diagnostic remains compact. This ABI 4 does not introduce deltas or
  freeze the future resource contract; subsequent incompatible changes need a
  different version. Recompile C/CFFI consumers; no compatibility alias remains.

- Share application declarations as `maelys_datalog_value_t`, `fact_t`,
  `predicate_t`, `domain_t`, `term_view_t` and `fact_view_t` (all with the
  `maelys_datalog_` prefix), without compatibility aliases. Rename native solver
  representations to `maelys_datalog_internal_*_t`. Prepared materialization
  consumes the common input facts directly; full-batch shape validation, typed
  identity, boolean normalization and bounded allocation-free execution remain.
- Use `maelys_datalog_limit_get` as the single C build-capacity accessor. Remove
  `maelys_datalog_build_limits_t` and `maelys_datalog_get_build_limits`; Python and
  Wasm read the scalar limits while preserving their existing language-level
  results. Rebuild C/CFFI consumers for the coordinated 0.10.0 source migration.
  These type-name changes alone preserve existing layouts and callback signatures;
  the coordinated diagnostic migration above requires backend ABI 4. They do not
  implement the future resource contract.

- Unify predicate declarations on `maelys_datalog_predicate_t` for stable
  and low-level domains and inline domain loading. Remove
  `maelys_datalog_predicate_def_t` and `maelys_py_predicate_def_t` without aliases;
  migrate declarations from `kind_flags` to `flags` and rebuild low-level/CFFI
  consumers together. The declaration migration changes the advanced interfaces
  while preserving stable C declaration layouts; the diagnostic migration above
  separately changes backend and program callback contracts.
- Separate declarations from private owned registry entries, retaining inline
  bounded names and existing registry/ruleset layouts. Registration copies input
  strings synchronously without allocation. The legacy Python shim and its
  redundant process-wide domain table are removed. Language bindings now use the
  public SDK as described above.

## 0.9.1 — 2026-09-23

One fix on the low-level domain registry. No public surface, behavior,
identity or ABI change for declarative consumers.

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
