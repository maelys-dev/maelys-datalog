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

A miss calls malloc once; a hit calls no engine allocator. Concurrent live
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
or per-session quota. On the qualified 64-bit layouts:

| Profile | Default borrowed-policy arena | Maximum retained idle bytes |
| --- | ---: | ---: |
| SMALL | 286,296 | 793,912 |
| LARGE | 476,760 | 1,140,024 |

These are native CMake layouts without `MAELYS_TESTING`; the SMALL Make guard
adds eight instrumentation bytes (286,304 / 793,920). Bounds are calculated
from the compiled layout, not hard-coded to these observations.

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

## Required measurements

Run the complete schema-4 Python protocol under default glibc against the
previous release and, separately, the immutable merged #142 baseline. Keep all
7/93-fact, symbol/integer, convenience/prepared, cold/warm and phase cases,
SMALL/LARGE, default/Release, the fixed positive and independent null controls.
Finish each run's builds before measuring and retain all existing budgets,
classifications and raw observations.

The ordinary previous-release report remains the release-review input. An
additional `comparison_base` full-SHA dispatch uses the same complete protocol
but is explicitly ineligible as standalone release evidence; it cannot replace
the published reference. Absolute timings are not comparable between runs.
A warm cache does not establish universal freedom from page faults or allocator
effects elsewhere in Python/CFFI. No gain or release acceptance is asserted
before examining these complete reports.
