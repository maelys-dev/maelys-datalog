# Fixed session resources: implementation and acceptance

This records steps 2 and 3 of the [reviewed 0.14.0 contract](../proposals/backend-session-resources.md),
based on `9338993f5eb82e928ebec99c385bef3df546c67f`. It does not declare 0.14.0
release qualification complete. The default ABI 5 declaration and callbacks stay
available; the distinct ABI 6 descriptor is now in `<maelys/datalog_backend.h>`
(originally `datalog_resources.h`; see the [include migration](../sdk-headers.md)).

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
uses one arena allocation on a miss (or reuses the bounded idle allocation
described by the [subsequent recycling change](session-recycling.md)). Program storage loaded in a caller buffer
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
It separately checks the cold constructor's one allocation and eventual free
after draining the idle-storage slot introduced after #142.
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

## Hosted native comparison

Signed candidate `f2372725bd077cd9211dc2f9f2a4eb77334061a1` retains byte-identical
runtime, headers, bindings and benchmark sources from `446c2c0`. Its
[CI run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36504147101)
passes all 17 jobs. The tested PR merge `eb28150` has exactly the same Git tree;
the installed matrices pass 40/40 in each profile on Linux and macOS. Hosted
mutation baselines pass 10/10 and all 14 mutations are detected per profile.

The [native run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36504180776)
compares base `9338993` with that candidate on an AMD EPYC 9V45 96-Core Processor,
Linux x86_64, Clang 18.1.3, `-O2 -UNDEBUG`, SMALL and LARGE. All builds finish
before the two A/A pairs and ABAB. The three timing reports reconstruct
byte-identically from the artifact CSVs; 1,098,048 individual input/session
samples reproduce their 3,648 summary rows. The legacy solver harness archives
matching CSV/JSON summaries, not individual solver samples; its quantiles cannot
be independently reconstructed from individual observations in this artifact.

| Surface / profile | Faster metric rows | Indeterminate | Slower |
| --- | ---: | ---: | ---: |
| Solver / SMALL | 43 | 54 | 2 |
| Solver / LARGE | 75 | 29 | 5 |
| Input / SMALL | 20 | 46 | 0 |
| Input / LARGE | 15 | 44 | 5 |
| Reused sessions / SMALL | 28 | 154 | 87 |
| Reused sessions / LARGE | 29 | 124 | 117 |

These are per-statistic observations, not independent trials or an aggregate
performance score. All classifications remain in the artifact, including the
input changes despite identical input source. No placement or hardware cause
is established by those timings.

For 1,000 disposable solves, median time is 2,216.946 → 1,036.357 µs in SMALL
and 3,072.455 → 1,058.087 µs in LARGE (ratios 0.4675 and 0.3444, respective A/A
floors 2.46% and 1.33%). The LARGE absent-predicate minimum is 2.093 → 0.203667 µs.
These within-run comparisons support the benefit of the changed disposable
initialization on those cases; they do not turn the independent ARM64 review
into a cross-run latency comparison. A contrary tail remains visible:
LARGE/repeated/10 p95 is 31.527 → 41.152 µs (+30.53%, floor 4.03%).

Reused sessions do not inherit that large benefit. For example,
LARGE/derive/reverse/integer/256 median is 98.779 → 109.0495 µs (+10.40%, floor
3.28%); LARGE/derive/sorted/symbol/128 p95 is +10.37% (floor 1.29%). The scoped
Callgrind diagnostic covers these cases and six predeclared controls, nine
cases total. Ir/Dr/Dw and exclusive instruction/write counts by function repeat
exactly twice per revision. Ir changes range from −1.438% to +1.252%; write
reference counts decrease in all nine. `solve_once_append_idb_merge` contributes
+33,407 Ir on the 256-integer derivations and +8,511 on the 128-symbol case,
partly offset by materialization changes. This localizes software work, but
does not explain a +10% time change or imply equal cycles. These counts cover
`solve_edb`, excluding creation, clocks, checks and release; they do not qualify
complete Python requests. Cache/branch events in that diagnostic are simulated.

Artifact ZIP SHA-256:
`5d4e48126749448a50afad4e2723400088c3bf74c1dbdb05a9836f5a20ace58e`.
Report hashes: `comparison.md`
`c4f99530329883a4db52bf5e6d60138d7a51ab4bf116c9608a32d384fc8b5949`;
`sessions.md` `a15ffb2a0ef0f821be7dc4be37425401af235a73b06c870c8e62001a1b2b344c`.

## Complete Python lifecycle review

The [Python run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36504178509)
measures the same signed `f2372725` against published v0.13.0 (`43bbde6`), the
immutable v0.11.1 anchor (`0f247a7`), and informative v0.11.0 (`e2c357e`). It uses
schema 4 on an AMD EPYC 9V74 80-Core Processor, hosted Linux x86_64, Clang 18.1.3,
Python 3.12.12, CFFI 2.0.0, both size profiles and CMake default/Release builds.
All builds precede measurement; independently sampled identical-binary nulls,
two A/A pairs and reversed variant order in the second comparison round are
retained. CPU models differ from the native run: their absolute timings must
not be compared.

The artifact ZIP digest matches the Actions API. All 11,424 raw process files,
3,377,136 flattened observations, 236 binary/header/binding file hashes, checked
outputs and telemetry are verified. Every candidate, null, positive and
historical statistic is recomputed from raw observations; candidate and null
matrices each contain 624 statistic rows. The 72 separate observer/storage
control files and the null-against-null cross-check also reproduce. No samples
are filtered, paired across total/phase loops, or normalized with telemetry.

The fixed three-request positive control is detected in **every one of the 32
case/configuration combinations, in both rounds**. This validates that coarse
injection, not sensitivity to small costs. The informative v0.11.0 control is
not detected in every configuration; it does not decide run validity.

The result is **`review_required`**, not performance acceptance: 110 warm
statistic rows exceed both their A/A floor and matching null envelope (87
against v0.13.0, 23 against the anchor). Among complete-request totals, 22 rows
against v0.13.0 and five against the anchor require review. Another 51 warm rows
have at least one raw alert not distinguished from their null; those remain
unresolved. Four cold above-floor rows remain informative. Counts refer to
statistics, not independent trials.

The most substantial repeated changes against v0.13.0 are on the SMALL
convenience path. The table gives complete-request **median** changes by round:

| Case | Round 1 | Round 2 | A/A floor | Matching null envelope |
| --- | ---: | ---: | ---: | ---: |
| SMALL default / 7 integer / solve | +59.66% | +59.74% | 2.27% | 2.27% |
| SMALL default / 7 symbol / solve | +58.70% | +60.04% | 1.23% | 2.17% |
| SMALL Release / 7 integer / solve | +67.48% | +63.97% | 4.78% | 3.72% |
| SMALL Release / 7 symbol / solve | +63.77% | +67.41% | 2.67% | 4.15% |
| SMALL default / 93 integer / solve | +8.98% | +7.86% | 1.32% | 1.17% |
| SMALL default / 93 symbol / solve | +8.46% | +7.11% | 0.74% | 1.09% |
| SMALL Release / 93 integer / solve | +10.96% | +11.92% | 0.91% | 2.98% |
| SMALL Release / 93 symbol / solve | +11.22% | +10.49% | 1.39% | 1.39% |

All eight rows exceed both controls in both rounds. LARGE convenience medians
are lower, by 1.99–18.24% across the corresponding cases/rounds; these do not
cancel the SMALL slowdowns. Prepared sessions have their own alerts, including
LARGE default / 7 integer median +5.18% / +4.33% (floor 1.75%, null 2.28%), and
SMALL default / 93 integer p95 +36.36% in round 1 (floor/null 27.27%), with round
2 indeterminate. All other phases, tails and anchor comparisons remain in the
unaltered report; none is discarded by this selection.

For SMALL Release / 7 integer / solve, median complete time is
80.461 → 134.753 µs in round 1 and 81.994 → 134.443 µs in round 2. The
same-transaction telemetry records **4 → 9,522 minor faults across 501 warm
requests in each round**, zero major faults and zero CPU-ID changes between
observed request boundaries. Candidate involuntary switches total six/eight,
reference zero/zero. Thread CPU medians rise as well (81.601 → 136.032 µs and
83.193 → 135.692 µs); those clocks bracket the request with observation overhead.
This establishes changed page-fault activity on that path, not a particular
allocator, syscall, frequency, placement or hardware cause. Boundary CPU IDs
do not prove that no migration occurred within a request.

Separate phase loops point to construction/solve and close: for that case,
`solve` medians are 15.644 → 50.777 µs and 16.705 → 51.908 µs; `close` medians
2.364 → 12.358 µs and 2.414 → 12.199 µs. Their samples are not the total-loop
samples and cannot be summed or paired by index. Python's convenience
`rules.solve()` constructs and owns a prepared public session per request; it
is not the bare disposable `maelys_datalog_solve_once` microbenchmark. The
native scoped counts above exclude construction/release and cannot explain
this complete-request slowdown.

The bounded observer/storage controls are also preserved: candidate medians
are indeterminate for both selected prepared cases; the reference observer
control has a second-round 93-integer median +4.79% (floor 4.16%). These controls
do not cover every workload or remove instrumentation effects. Cross-screening
the two null references itself produces alerts (32 and 29 rows in the two
directions among 284 matching warm rows). It does not estimate the candidate's
false-positive rate or erase its original 110 review triggers.

Report `report.json` SHA-256:
`1fa3e1ceae271b226cb695d5bb1421688ff3d552dce6df69340714ea2480d20b`.
Artifact ZIP SHA-256:
`86acfc07f8a1d0fe81209e417dd918690fa966fe202f7f67ebc939e5659539f0`.

## Hosted allocator-policy confirmation

The [bounded allocator run](https://github.com/maelys-dev/maelys-datalog/actions/runs/36535239421)
uses signed diagnostic `2873989cb918fe8fdde148b4071afdf26e6e6606`. It reuses the
original `43bbde6` and `f2372725` binaries and unchanged `7-integer-solve`
workload, without rebuilding. The pre-PR `9338993` main has identical runtime,
installed-header and binding sources to the published reference. The runner is
AMD EPYC 9V74, Ubuntu glibc 2.39-0ubuntu8.9, Python 3.12.12. Its same CPU model
does not permit absolute latency comparison with the original run.

Both SMALL build modes compare four conditions: base/head with default glibc,
and base/head with exactly `MALLOC_TRIM_THRESHOLD_=268435456
MALLOC_TOP_PAD_=67108864`. Two A/A pairs per condition precede four rounds with
balanced condition positions. All 64 raw processes, 160,320 phase/total sample
values, 58 preserved binary/header/binding hashes, outputs and 24 statistical
comparisons are independently checked. The original report and classifications
are unchanged; the diagnostic is not release-eligible.

Ranges below are **process medians across the four comparison rounds**;
minor faults count the 501 warm total requests within each process:

| SMALL build | glibc condition | Base median range (µs) | Candidate median range (µs) | Base minor faults | Candidate minor faults |
| --- | --- | ---: | ---: | ---: | ---: |
| default | default | 70.434–72.707 | 117.504–120.148 | 4 | 9,522 |
| default | explicit two-variable treatment | 70.394–72.347 | 70.254–72.838 | 4 | 3 |
| Release | default | 61.380–63.734 | 103.413–109.802 | 4 | 9,522 |
| Release | explicit two-variable treatment | 60.319–62.042 | 58.516–59.288 | 4 | 3 |

With default glibc, candidate/base median gaps are +65.00% to +70.58% in the
default build (A/A floor 7.11%) and +68.15% to +72.28% in Release (floor 1.62%):
all eight round comparisons are slower. Under the treatment, the default-build
gaps are −2.72% to +3.47%, all indeterminate at floor 4.54%; Release gaps are
−5.68% to −2.36%, three indeterminate and one faster at floor 4.83%. The base's
own treatment gaps are indeterminate in every round. The candidate's treatment
reduces its median by 38.01–46.71%, always above the corresponding A/A floor.
P95 and minimum classifications remain in the artifact, including unresolved
tails; convergence of medians does not establish equality of distributions.

This controlled intervention reproduces and removes the large default-policy
cost while reducing the candidate's warm minor faults from approximately 19 per
request to three across the entire 501-request series. It establishes allocator
policy as a causal factor in the observed cost for this case. The reviewer also
reports a local Docker ARM64/glibc 2.39 reproduction with the default ordering
reversed (base slower) and converging medians under these variables; that is
independent reported evidence, not a cross-machine latency comparison.

Eight additional processes run under `strace`, only after timing completes.
All 501 marked warm transactions per process have no `brk`, `mmap`, `munmap` or
`madvise` calls, and their telemetry has zero minor faults. The trace parser and
raw marked regions are independently checked. Thus these differently observed
processes do **not** reproduce the ordinary candidate's faulting regime; their
times are unused. They cannot identify which syscall returned pages in the
untraced processes. Their import/marker/tracing context also differs, so this
does not isolate one observer effect. The two-variable intervention changes
arena retention and adaptive allocator behavior; it does not distinguish
trimming from every other allocation-policy contribution. The native scoped
instruction counts exclude session construction/destruction and cannot support
the broader claim that complete engine/request work is identical.

Report SHA-256:
`1a91654b4958639f05cf4dda4067cba6cd6631ab50c2bbd1ee1175dca507e6e2`.
Artifact ZIP SHA-256:
`c1dd4ffcb5b6ca88c5d1f6854cada7b5166e080265b7dc9cbb94ed34652db429`.
The 59 Python-performance tooling tests pass, including rejection of changed
original evidence, inherited tuning, incomplete traces and incorrectly scoped
requests. [CI 36535218644](https://github.com/maelys-dev/maelys-datalog/actions/runs/36535218644)
passes all 17 jobs on the signed diagnostic head. No production allocator
setting, workload threshold or noise budget
has changed. A mitigation through reuse of a prepared Python session belongs
in a separate binding PR outside 0.14.0, with its own lifecycle/lease and
performance qualification; it is not implemented here.

## Maintainer decision: merge only, release remains blocked

Step 3 now supplies the installed matrix, downstream replay, reservation
inventory and complete native/Python measurements. Functional qualification
passes. The allocator intervention now removes the dominant seven-integer
convenience cost on the hosted runner, with the attribution limits above; it
does not investigate every other original phase, prepared-session or tail
alert. The original schema-4 report remains `review_required`. On 2026-09-29,
David accepted the documented tradeoff on report `1fa3e1ce…`, informed by
diagnostic `1a91654b…`, for merging #142 only. This is not release approval.
The 0.14.0 cut waits for a separate session-storage recycling PR, its functional
and allocation qualification, and a complete default-glibc Python replay on
the baseline and candidate, including 93-fact and prepared-session cases. The
bounded seven-integer control does not replace that replay. Its concrete report
still needs a maintainer release decision; workflow success is not approval.
Any runtime, binding, build or ordinary benchmark correction requires new
measurements; a later quiet run cannot erase the original. No allocator
service, delta API or private-provider support claim is introduced here.
