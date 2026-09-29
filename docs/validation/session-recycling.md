# Bounded recycling of native session storage

This follows #142 on `41c8857838beaa30ba845381dbc7d40d7d11cc46`, accepted for
merge only. The 0.14.0 cut still waits for this separate change, complete Python
measurements and a maintainer decision on those reports. No malloc policy changes.

## Ownership and bounds

The legacy public constructor previously allocated three blocks: public session
metadata/results, prepared inputs, and the solver workspace. The 156,888-byte
SMALL allocation observed in the investigation was only one of them; the
Make allocation guard measured 286,304 bytes in total with `MAELYS_TESTING`.
These regions now share one
aligned allocation. Consolidation is needed to eliminate all three session
allocator calls on a hit; result semantics and backend ABI 5/6 do not change.
No legacy provider is migrated to ABI 6.

One native library instance retains at most one idle raw allocation, shared
across threads. An equal-size creation removes it atomically and reinitializes
all live metadata, dictionaries, program references and workspace validity.
No prepared session, result, callback state or policy reference remains live
in the slot. Unused payload is not exposed or erased: old bytes may remain
until overwritten. No secure-erasure guarantee is added.

A host-arena miss calls malloc once; a hit acquires it without an allocator call.
The guarded reference constructor makes no other engine allocator calls on
that warm hit. Concurrent live
sessions need distinct arenas. Result/explanation leases prevent destruction
and recycling. Different sizes can miss; the next eligible close replaces the
idle block and frees its predecessor. This guarantees sequential compatible
reuse with an available block, not allocator independence for arbitrary
workloads or zero Python allocation. Policy loading, optional explanation
allocations and provider-owned resources are accounted separately.

Only engine-owned host arenas participate. Caller arenas/provider/explanation
buffers never do. Fixed-resource convenience arenas share the same byte bound;
provider requirements cannot enlarge it. The bound derives from the profile's
largest legacy host arena (export buffers plus a copied policy), not a per-thread
or per-session quota. On the qualified macOS arm64 / Apple Clang layouts:

| Profile | Default borrowed-policy arena | Maximum retained idle bytes |
| --- | ---: | ---: |
| SMALL | 286,296 | 793,912 |
| LARGE | 476,760 | 1,140,024 |

These are native CMake layouts without `MAELYS_TESTING`; the SMALL Make guard
adds eight instrumentation bytes (286,304 / 793,920). The hosted Linux x86_64
Clang guard without that definition observes 286,304 / 793,920 in SMALL and
476,768 / 1,140,032 in LARGE. Bounds are calculated from the compiled layout,
not hard-coded to these observations.

Retention is additional to live-session storage plans, allocator metadata and
other process memory. A replaced block is detached under the lock and freed
outside it; concurrent closes may briefly have detached blocks awaiting free.
Only one remains idle afterward. No allocator or provider callback executes
under the slot lock.

Provider destruction, native context release, explanation cleanup, arena
unregistration and policy release finish before storage becomes idle. Failed
constructors free their acquired block. Creation initializes validity without
relying on old allocator contents. Closing adds no bulk reset.

Native GCC/Clang builds drain the slot at shared-library unload or normal
process teardown, then prevent later destructors from refilling it. Callers
must finish library calls before unloading. No public purge API is added;
qualification uses a hidden, uninstalled hook. Builds without this native
destructor mechanism, including Emscripten, consolidate but do not cache.
Emscripten's default live runtime cannot provide the native teardown guarantee.
No process-wide tuning, thread-local pool or growing cache is introduced.

## Qualification

The all-engine guard covers cold/warm creation with the heap disabled, different
programs, exact sizes, legacy/fixed constructor transitions, caller storage,
oversized providers, live-result rejection, failed preparation, reentrant
destruction and four concurrent creator threads. Existing rollback/window,
explanation and SDK contracts remain. Cold failure injection explicitly purges
first so a hit cannot mask an allocation failure.

Nine negative controls cover: allocating on a hit, wrong-size reuse, missing
slot detachment, lost eviction free, over-bound retention, caller-storage
retention, live-result reuse, omitted provider destruction and omitted unload
cleanup. Build failures do not count as detection. The first eight run under
ASan/UBSan; the ninth exercises real dlopen/dlclose and allocator accounting
across three independent loads. Raw logs/patches stay outside Git and in CI.

Local SMALL Debug and LARGE Release pass all 29 CMake suites, also under
ASan/UBSan in those configurations; both profiles detect all nine negative
controls. The four-thread suite also passes ThreadSanitizer locally.
Further sanitizer/CI results and signed revisions are recorded in the PR.
Early fixture enum/domain declarations, a private include path and cold-test
cache preconditions were corrected; the native WASM transport probe separately
checks and drains its one retained block before its final leak assertion.
The complete Make check passes as well. Initial failure logs, including an
unsupported absolute Make build-directory invocation, are preserved.
The first CI also exposed a missing guarded-object selection in `Makefile.asan`
and a test assumption that default legacy/fixed arenas have identical sizes.
The latter depends on target alignment: cross-constructor hits are required
only when their actual byte sizes match; mismatches must miss. Both controls
were corrected without changing the recycling implementation.

Signed `646ac3d76916bc20a990bf08deea37fea4c56fde` passes all **17 CI checks** in
[run 36545450500](https://github.com/maelys-dev/maelys-datalog/actions/runs/36545450500).
The tested merge `8604c0397e7397f6c7a41c291519bab9e2bd9e49` has the candidate's
exact tree `4dfb45280ac6aa3dff8924ecfecce7389daa4e82`. The raw artifacts verify
nine recycling and fourteen fixed-resource mutations in each profile, baseline
success and three real unloads per profile. Linux/macOS × SMALL/LARGE each
verify 38 separately compiled caller/provider combinations and two expected
new-API/old-host link rejections. Both Emscripten versions pass their two profiles.

A fresh installed LARGE Release SDK from the same signed archive passes the
private ABI 5 consumer at `608aa9e2485a8d06350850314720fecdc5f90991`: 10/10 tests
normally and 10/10 with allocation hooks across the entire SDK and consumer.
No consumer source or pin migration is needed. An initial local command used
an unrecognized CMake profile option and built SMALL; the consumer correctly
rejected it. That log and the corrected fresh LARGE builds remain preserved.

## Preserved pre-recycling report

The automatic [merged-#142 run 36542767705](https://github.com/maelys-dev/maelys-datalog/actions/runs/36542767705)
measures `41c8857838beaa30ba845381dbc7d40d7d11cc46` against published v0.13.0,
with all 32 positive-control case/configuration pairs detected in both rounds.
Independent reconstruction checks 11,424 raw files, 3,377,136 flattened samples,
236 binary/header/binding hashes, 624 comparison rows and matching null rows.
It remains `review_required`: 111 warm statistic rows exceed both A/A and their
matching null envelope, including 28 complete-request rows.

In this run, SMALL Release / 7 integer / convenience medians are
109.055 → 153.959 µs and 110.097 → 157.215 µs (+41.18% / +42.80%; A/A 1.33%,
null 2.27%). The same total-loop transactions record 4 versus 9,522 minor
faults over 501 requests in both rounds. SMALL convenience / 93 facts also
retains above-envelope median observations: +3.97% to +8.66% across the four
case/configuration pairs and two rounds. This is another observation of the
pre-recycling path, not a comparison with the recycling candidate. Absolute
times are not compared across runs, and the earlier #142 reports stay intact.

Report SHA-256: `6ce8a157ff26ed57b7f125d539cf01d3e3b00b9f408677bf0c97b4a21f482795`.

## Complete comparison with merged #142

[Run 36545921158](https://github.com/maelys-dev/maelys-datalog/actions/runs/36545921158)
compares signed `646ac3d76916bc20a990bf08deea37fea4c56fde` with merged #142
`41c8857838beaa30ba845381dbc7d40d7d11cc46`. It uses the full schema-4 corpus,
default glibc policy, AMD EPYC 7763, Clang 18.1.3 and Python 3.12.12. Every
build finishes before the two A/A pairs and counterbalanced comparison rounds.
The immutable v0.11.1 anchor, informative v0.11.0, independent nulls and fixed
three-request positive control remain present. All 32 positive-control
case/configuration pairs are detected in both rounds.

Independent reconstruction verifies 11,424 raw files, 3,377,136 CSV samples,
240 binary/header/binding hashes and all 632 comparison rows and corresponding
null rows, including observer/storage controls and unchanged output hashes.
This additional comparison is marked `comparison_only=true` and
`release_eligible=false`; it cannot replace the previous-release report.

For convenience calls, every median below is faster in both rounds beyond its
own A/A floor. Ranges retain both CMake modes, integer/symbol input and rounds:

| Profile / facts | Median change vs #142 | Minor faults per 501 complete requests, #142 → recycling |
| --- | ---: | --- |
| SMALL / 7 | −37.79% to −33.68% | 9,522 → 3 |
| LARGE / 7 | −56.20% to −47.64% | 27,558 → 3 |
| SMALL / 93 | −9.01% to −5.93% | integer 10,021 → 1; symbol 10,524 → 3 |
| LARGE / 93 | −17.73% to −11.97% | integer 30,562 → 1; symbol 30,564 → 3 |

These are measurements on this run, not universal latency or page-fault
guarantees. The native allocation guard independently establishes zero engine
allocator calls on the available equal-size warm hit. Python/CFFI still allocate.

No prepared-session median is slower beyond both its A/A floor and matching
null envelope. This is not evidence of zero overhead: raw above-floor medians
screened by the null remain in the report. Three complete-request **p95** rows
against #142 still require review, each in round 2 (round 1 is indeterminate):

| SMALL Release / prepared | Round 1 / round 2 | A/A floor | Null envelope |
| --- | ---: | ---: | ---: |
| 7 integer | +0.04% / +5.11% | 3.54% | 3.54% |
| 7 symbol | −0.03% / +2.94% | 1.66% | 2.69% |
| 93 integer | −0.67% / +2.41% | 1.01% | 1.01% |

The complete report remains `review_required` with 21 warm statistic rows
across total and separately measured phases, including those three totals.
The prepared total loops have zero minor faults in both revisions. These p95
observations are retained without allocator, placement or hardware attribution;
phase samples are not paired with total samples. No timing threshold changes
and no further runtime fix follows from those timings alone.

Report SHA-256: `4f5862024c6c7a2cf6e9bda548d66350319f3a4a8747a75fbc94613254cdad6c`.

## Complete comparison with published v0.13.0

[Run 36545968747](https://github.com/maelys-dev/maelys-datalog/actions/runs/36545968747)
measures the same signed candidate against v0.13.0
`43bbde637435e5f6fb4756fb22c25f18f8f30583`. This is the ordinary release-review
report (`comparison_only=false`, `release_eligible=true`), not release approval.
Its runner is Intel Xeon Platinum 8573C, Linux x86_64/glibc 2.39, Clang 18.1.3,
Python 3.12.12, CFFI 2.0.0, with default allocator policy. It retains the same
complete corpus, two build modes/profiles, anchors and controls. Every build
finishes before measurement; all 32 positive-control pairs pass both rounds.

Independent reconstruction verifies 11,424 raw files, 3,377,136 CSV observations,
236 binary/header/binding hashes, all 615 comparison and matching null rows,
observer/storage controls and output hashes. The different number of statistic
rows follows the existing minimum-versus-median/p95 selection at 10 µs; no
case is removed. The report remains **`review_required`**: 49 warm phase/total
rows against v0.13.0 and 22 against the anchor, including 11 and 3 total rows
respectively. Counts of alerts are not an effect-size comparison between runs.

Convenience-call medians in LARGE improve beyond A/A in both rounds for all
case/configuration pairs: −65.07% to −54.76% for 7 facts, −17.43% to −12.89%
for 93. The corresponding total loops record 37,579 → 3 minor faults per 501
seven-fact requests, and 39,581 → 1 (integer) / 39,583 → 3 (symbol) at 93 facts.
In SMALL, v0.13.0 already records only 2–4 minor faults, versus 1–3 for the
candidate. SMALL Release seven-fact medians improve by −3.56% to −1.90% beyond
A/A; the default-build seven-fact comparisons are indeterminate. SMALL/93 is
mixed, including the +0.97% first-round integer convenience alert below.
These observations do not transfer absolute times or allocator regimes between
the EPYC parent-comparison run and this Xeon run.

Every complete-request row requiring review against v0.13.0 is retained here.
Percentages are candidate/reference minus one; an alert in either round is
enough for review. Rounding is increased for the barely beyond-null 7-integer
SMALL Release median so the boundary is visible.

| Case | Statistic | Round 1 / round 2 | A/A floor | Null envelope | Beyond both in round |
| --- | --- | ---: | ---: | ---: | --- |
| LARGE Release / 7 integer / prepared | median | +2.61% / +1.28% | 1.68% | 1.68% | 1 |
| LARGE Release / 7 symbol / prepared | median | +2.78% / +0.53% | 2.35% | 0.77% | 1 |
| LARGE Release / 7 symbol / prepared | p95 | +39.85% / −1.07% | 21.55% | 2.75% | 1 |
| SMALL Release / 7 integer / prepared | median | +1.5344% / +1.3647% | 0.6168% | 1.5297% | 1 |
| SMALL Release / 93 integer / prepared | median | +4.29% / +2.68% | 1.97% | 0.83% | 1, 2 |
| SMALL default / 7 symbol / prepared | median | +0.13% / +21.75% | 2.36% | 1.88% | 2 |
| SMALL default / 7 symbol / prepared | p95 | −1.48% / +67.86% | 3.38% | 2.15% | 2 |
| SMALL default / 93 integer / prepared | median | +1.84% / +3.12% | 0.81% | 1.08% | 1, 2 |
| SMALL default / 93 integer / prepared | p95 | +1.42% / +6.28% | 1.63% | 1.63% | 2 |
| SMALL default / 93 integer / convenience | median | +0.97% / +0.56% | 0.65% | 0.55% | 1 |
| SMALL default / 93 symbol / prepared | p95 | +24.09% / −0.64% | 17.32% | 17.32% | 1 |

The three total alerts against the v0.11.1 anchor are LARGE Release / 7 symbol /
prepared p95 (+32.30% / −2.80%, A/A 21.55%, null 4.80%); SMALL default / 7 symbol /
prepared median (−3.38% / +18.31%, A/A 2.36%, null 1.47%) and p95
(−4.37% / +64.96%, A/A 3.38%, null 2.48%). All cold observations and every
separately measured phase remain in the original artifact.

Prepared total loops record zero minor faults in both revisions. Their alerts
therefore cannot be declared resolved by the convenience-path fault reduction.
The two-round SMALL/93 integer observations and the larger one-round bursts
remain unattributed; neither a quieter parent comparison nor the identical-
binary null cancels them. No phase/total samples are paired, no latency is
normalized by telemetry, and no hardware/placement explanation is inferred.

Report SHA-256: `a73b1cfc0ff1927776f107f6ea489a4800d5d8d3316d10b4b5518be4bcffc4b7`.

## Decision still required

The two requested complete default-glibc replays are finished and independently
verified. They establish the measured convenience improvement and preserve the
remaining alerts, including 93-fact and prepared sessions. A warm cache does not
establish universal freedom from page faults, allocator effects elsewhere in
Python/CFFI or timing regressions.

The final evidence-only commit changes no measured runtime, binding, build or
harness code; `646ac3d` remains the measured revision. Earlier #142 reports,
the merged baseline report and both recycling reports keep their original
classifications. No threshold, budget, reference or allocator policy is changed
to accept the candidate. David's merge-only acceptance of #142 is not approval
of #143 or of these remaining observations. The recycling PR is prepared for
review; **0.14.0 remains blocked on its merge and a maintainer decision on the
concrete complete Python reports**.
