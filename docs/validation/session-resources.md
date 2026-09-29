# Fixed session resources: implementation and acceptance

This is step 2 of the [reviewed 0.14.0 contract](../proposals/backend-session-resources.md),
based on `9338993f5eb82e928ebec99c385bef3df546c67f`. It does not declare 0.14.0
release qualification complete. The default ABI 5 declaration and callbacks stay
available; the distinct ABI 6 descriptor is in `<maelys/datalog_resources.h>`.

## Admission and ownership

Requests normalize E/D/S/T once per plan. Missing fields take loaded-profile
defaults, zero is exact, above-profile values are rejected, and compiled symbol
roots must fit S/T. The program's build/per-predicate bounds are unchanged.
Both ABI 6 requirements and preparation receive the same normalized record.
Explicit ABI 5 selections reject non-default quotas or an explicit resource
feature requirement; they are never migrated by matching names or callbacks.
The reserved allocator bit and elastic mode are refused before provider calls.

Caller construction recomputes the plan and checks arithmetic, alignment,
short buffers, overlaps with its arguments and known live session ranges.
Reservations are recorded before provider preparation, so reentrant creation
cannot reuse an arena still being initialized. The registry reads immutable
planned ranges, without following concurrently initialized input pointers.
It allocates and frees no engine storage. The convenience resource constructor
uses exactly one arena allocation. Program storage loaded in a caller buffer
is copied into that arena; an owned policy is retained instead. External
provider and explanation storage count once, by required slice length.

The native input pool reserves `min(B, E + F)` facts, where B is the build EDB
bound and F is the compiled fact count. F does not consume the raw dynamic
input quota E. Sort scratch uses raw term occurrences, not S. Native IDB and
proof indices reserve D entries; external result emission has the same D bound,
including auxiliary relations. Export storage is reserved only when the solve
callback needs it. There is no growth or allocator fallback after construction.

This implementation retains a fixed storage floor: the complete dictionary and
hash index, EDB insertion/pair scratch, and provenance structures still use
build-profile reservations. E/D payloads are sized. S/T are exact admission
bounds but reducing them does **not** currently shrink dictionary bytes.
The returned plan includes this floor, padding, policy retention/copy and the
selected explanation storage; it is not the earlier proposal's payload estimate.
`tools/session_storage_inventory.sh SMALL` and `LARGE` report actual layouts
and default reservations. The resource test prints plans for a smaller vector.

## Behavioral evidence

### Disposable solver workspace

`solve_result_acquire(NULL)` formerly allocated the entire disposable
`solve_once` workspace with `calloc`. It now calls `solve_workspace_create(0)`:
`malloc`, then `maelys_datalog_solve_workspace_init` zeros only the metadata
prefix and initializes the payload pointers and derived capacity. This is an
observable change to the allocating solver microbenchmark, independently of
selectable quotas. It does not remove an allocation.

Safety depends on validity, not on old allocator contents: counts and ranges
start at zero, the EDB snapshot copies only its valid facts, IDB facts are written
before increasing the live range, and all derived proof indices are initialized
to `MAELYS_DATALOG_PROOF_NO_PARENT` before solving. Proof initialization resets
its counts; each new provenance node has its witness range invalidated before
use, and the witness mask is reset for each rule application. The symbol pointer
is assigned on every solve. Unused payload is neither enumerated nor treated as
live. Owned results are freed directly; reusable workspaces retain the existing
metadata-reset contract. Ordinary `memset` is not secure erasure.

David's independent local replay compared `9338993` with `446c2c0` using
`LIFECYCLE=1 bash bench/compare_revisions.sh 9338993 446c2c0 /w/out` in
`maelys-host-delta-tools:ubuntu24.04` (Linux ARM64 under linuxkit, Clang 18).
It used two A/A pairs then ABAB, 1,000 samples per case. The reported timings
below are from `comparison.md` and raw pass CSVs in the review session's
`scratchpad/pr142-docker/out/`; they are local review evidence, not a hosted run
or instruction counts. Callgrind in that replay covered session lifecycle only.

| Solver case | Profile / statistic | Base (µs) | Candidate (µs) | Candidate/base |
| --- | --- | ---: | ---: | ---: |
| repeated solve, 1,000 calls | SMALL / median | 4,580.97 | 1,434.39 | 0.313 |
| repeated solve, 1,000 calls | LARGE / median | 5,596.19 | 1,448.52 | 0.259 |
| repeated solve, one call | LARGE / minimum | 4.000 | 1.416 | 0.354 |
| absent predicate, 512 | LARGE / minimum | 2.792 | 0.250 | 0.090 |
| selectivity, 64 / 0.015625 | SMALL / minimum | 2.625 | 0.958 | 0.365 |

The repeated 1,000-call totals are about 3.2–3.9 times shorter in this run;
the absent-predicate minimum is about 11 times shorter. The removed full-workspace
zeroing is a concrete fixed-cost change on this path. These timings alone do not
isolate every contribution to the ratios or establish an algorithmic speedup.
The review reports no corresponding above-noise change in the reused-session
`sessions.md` measurements. Complete-Python qualification is separate.

### Contracts and negative controls

`test_maelys_datalog_session_resources` covers versioned prefixes/tails,
reserved-feature refusal, a recording ABI 6 provider, two coexisting vectors,
unchanged program bounds, default identity, exact zero bounds, raw duplicates,
auxiliary IDB accounting, rooted symbols, distinct E/D/S/T diagnostics, retry,
checked plans, external arenas, live overlap, prepare failure, both policy
ownership paths, typed registry rollback and wrong-ABI name refusal. A group
window rejects unequal resource fingerprints and preserves its committed result
on a late explanation-lease rejection with equal quotas.

The allocation-guard build disables engine allocation throughout caller arena
initialization, solve, query, explanation preparation/release and caller close.
It separately checks the allocating constructor's one allocation/one free.
Existing dictionary rollback, collision, canonical-ID, result-reset and window
tests remain in the native and sanitizer inventories; their budgets are not
relaxed. Python fixed-capacity tests run against installed SDKs, with default
constructor/call-budget and fingerprint tests retained. Python/CFFI still allocate.

`tools/check_session_resource_mutations.py --profile SMALL --output /tmp/resources-small`
(and `LARGE`) builds a passing ASan/UBSan baseline, then independently recompiles
each altered translation unit. Build failures are not detections. The witnesses
reject quota smuggling through `program_info`, accepted elastic/reserved features,
ABI 5 non-default admission, altered preparation resources, missing F reservation,
ignored S/T/D quotas, unchanged identity for changed quotas, missed live overlap,
unchecked arithmetic and commit after rejection. CI retains logs and results as
artifacts. An early missing-F mutant survived a result-only test because compiled
facts can be read directly by the solver; a storage-plan assertion now detects
that incorrect reservation as well. Preserve both runs when reviewing evidence.

## Separate-build qualification

`tools/check_session_resource_history.sh BUILD SMALL NEW_ABSOLUTE_OUTPUT`
(and `LARGE`) builds and installs the frozen pre-ABI-6 base `9338993`, installs
the candidate in a fresh prefix, then runs
`tools/check_session_resource_matrix.py --old OLD_PREFIX --new NEW_PREFIX
--output NEW_ABSOLUTE_OUTPUT`. Fixture sources are copied outside the repository;
caller and provider objects are compiled independently with installed headers
only, without LTO. The same objects are relinked against each host.

Each profile has 40 cases:

- 16 ABI 5 combinations: old/new caller headers × old/new provider headers ×
  old/new host × static/shared. Program bounds, reference and external-provider
  execution fingerprints, full output, result leases, commit/release and destroy
  callbacks must agree across all combinations.
- 16 ABI 6 combinations: base/optional-tail caller × base/optional-tail provider
  × old/new ABI 5 provider object × static/shared new host. Non-default quotas
  must refuse ABI 5 before prepare; ABI 6 receives identical normalized values
  in requirements/prepare, with unchanged program bounds. ABI 6 did not exist
  in the old release: these tail variants test V1 record evolution, not a claim
  of a released older ABI 6 provider.
- Two negative links: a new resource-API caller cannot link against the old
  host, whose symbols do not implement that API. This is an expected link error,
  not runtime fallback or dynamic negotiation.
- Six layout probes: C11/C++17 under SMALL/LARGE/synthetic-XLARGE compilation
  labels. Resource record sizes, alignments and offsets must be identical, and
  quota/plan/count members must have type `size_t`.

The synthetic boundary first transports E=4097, D=4099, S=8193 and T above
`UINT32_MAX` on 64-bit hosts, then varies all four quotas above that boundary.
Separately compiled consumers/providers negotiate these against supplied
synthetic resource ceilings and program bounds, without allocating quota-sized
arrays; exceeding a supplied ceiling is refused. It checks optional tails, short records,
unknown version/required features and reserved mode/allocator refusal. This is
not an XLARGE engine: the actual SMALL/LARGE host must still reject a request
above its loaded profile before calling prepare. The ordinary resource suite
also tests real short-prefix requests and output buffers against the host.
Commands, all logs, object/library hashes and the result manifest are artifacts.

The installed resource declarations contain no profile-sized arrays or masks.
The existing `MAELYS_DATALOG_IR_MAX_*` constants still bound the language/IR
representation; they are not reinterpreted as session E/D/S/T quotas. This audit
does not widen the engine's internal symbol, predicate or proof indices.

Local qualification of the unchanged runtime at `446c2c0`: 40/40 matrix cases
in SMALL and LARGE, with the historical-build wrapper also replayed. An
independent fixture mutation narrowing the transported T to `uint32_t` is
detected by the high-value assertion (passing unmodified baseline retained).
Early fixture attempts used an invalid hyphenated provider name and omitted the
advanced header; their admission/compile failures are retained as tooling
corrections, not engine failures or mutation detections.

The downstream ABI 5 consumer at `608aa9e2485a8d06350850314720fecdc5f90991`
was rebuilt from an isolated source archive against a clean installed LARGE
candidate SDK: Release 10/10, ASan/UBSan Debug 10/10 and Release 10/10. Both SDK
and consumer were instrumented in the sanitizer runs. No source migration was
needed relative to its existing v0.13.0 includes; its dependency pin and source
were not edited. This qualifies its existing default ABI 5 path, not selectable
private quotas, ABI 6 support or every additive window operation. Apple ASan
does not qualify LeakSanitizer here. A fourth installed LARGE replay instruments
allocators in every SDK/consumer translation unit using the consumer's test
guard: 10/10, including disabled-allocation execution and failure/rollback
witnesses. These test hooks are not shipped. The ordinary reproduction uses:

```sh
cmake -S ENGINE_SOURCE -B SDK_BUILD -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DMAELYS_DATALOG_PROFILE_LARGE=ON \
  -DCMAKE_INSTALL_PREFIX=FRESH_PREFIX
cmake --build SDK_BUILD --parallel 4
cmake --install SDK_BUILD
cmake -S CONSUMER_SOURCE -B CONSUMER_BUILD -DCMAKE_BUILD_TYPE=Release \
  -DMAELYS_SDK_PREFIX=FRESH_PREFIX
cmake --build CONSUMER_BUILD --parallel 4
ctest --test-dir CONSUMER_BUILD --output-on-failure
```

Sanitizer variants add `-fsanitize=address,undefined -fno-sanitize-recover=all
-fno-omit-frame-pointer` to `CMAKE_C_FLAGS` on both builds, once with Debug and
once with Release. The private source and generated results remain outside the
public repository. The main native `make check` and 53 Python-performance
tooling tests also pass; these are functional/tooling results, not timings.

## Remaining release qualification

Step 3 requires this matrix, downstream installed SDK replay, and measured
reservation/performance evidence on the signed candidate. The current Python
exposure selects fixed quotas only. No smaller
reservation, passing allocator test or software instruction count alone proves
speed. Native and complete-Python same-run controls, raw classifications and the
maintainer's release decision remain required by the existing protocols. This
step introduces no allocator service, delta API, private-provider claim or release.
