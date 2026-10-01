# Optional backend allocation — revised contract proposal

Status: documentation proposal, 2026-10-01, against the v0.17.0 SDK. Allocation
is still unsupported. The filename records an earlier 0.15.0 schedule; that
release shipped the common JavaScript binding. This revision does not commit to
a release number, install a declaration, implement a service or change an ABI.

The [fixed-capacity contract](backend-session-resources.md) remains in force:
FIXED is the only admitted mode, and the reserved `CALLER_ALLOCATOR` feature and
`BACKEND_ELASTIC` mode are rejected, even when a provider advertises them. The
future service extends the unchanged ABI 6 resource prefix; adopting ABI 7 input
delivery is a separate decision. Neither callback signature nor language
capability changes. An ABI 7 provider would negotiate these same resources
independently of its transaction packets; its base advances only on commit.

This revision decides six prerequisites before implementation:

| Prerequisite | Decision |
| --- | --- |
| Affordable growth before a sticky refusal | A side-effect-free inspection returns the exact charged size, current charge and remaining margin. It uses acquisition's arithmetic; sufficient margin does not promise allocator success. |
| Abort and retained capacity | Strict abort returns every new provisional block and restores preexisting ownership/current charge. It cannot create a new cache. Previously retained idle blocks remain charged and may be reused. |
| Cap and execution identity | The normalized finite cap enters an elastic fingerprint. Both window banks must therefore have equal configured caps, accounted independently. |
| Caller/service declarations | [Review-only C records](backend-allocation-v1.h) define sizes, versions, stable prefixes and callback types. No installed header changes. |
| Growth lifetime | Acquire during prepare/solve, install at infallible commit, release superseded blocks at accepted result cleanup after all leases. The old plus new peak is charged. |
| Refusal status | Cap exhaustion is `PAYLOAD_TOO_LARGE`; caller allocator NULL is `STORAGE_TOO_SMALL`, field `allocator`. The first acquisition failure is sticky. |

The public host owns this service and its small conformance provider. A separate
consumer experiment validates real storage needs against an installed SDK after
implementation. Accepting this document is neither production-backend
qualification nor a claim that a test provider proves all consumer behavior.

## 1. Versioned declarations and admission

The [compilable proposal header](backend-allocation-v1.h) lives under `docs/` and
is not part of any build or SDK installation. It uses native `size_t` byte counts,
`uint32_t` versions and `uint64_t` feature masks; boundaries use `sizeof` and
`offsetof`, not fixed LP64 constants. Its declarations are review material until
a separately reviewed implementation makes them available.

`caller_allocator_t`, `allocation_service_t` and `allocation_budget_t` start with
`struct_size`, `contract_version`, zero `reserved` and `required_features`.
Version 1 requires the complete declared record and zero descriptor feature
masks. Unknown versions/features return `UNSUPPORTED`; a short record, invalid
known boundary, nonzero reserved field or missing callback is `INVALID_ARGUMENT`.
Outputs shorter than V1 return `STORAGE_TOO_SMALL` without payload modification.
Larger compatible records may carry optional tails: readers access only known
members, and outputs preserve caller size and unknown tail bytes. Never copy an
entire local structure before validating the caller's readable extent.

The larger `session_allocation_request_t` contains the existing request as its
first member, followed by `execution_byte_cap` and the caller descriptor pointer.
Its `base.struct_size` describes the complete extended object;
`base.contract_version` stays 1. The caller selects `BACKEND_ELASTIC` and requires
`CALLER_ALLOCATOR`; neither a pointer nor a cap alone opts in. A missing/short
required tail, zero cap or missing allocator is invalid. FIXED with this required
feature is unsupported. FIXED without it continues to ignore optional unknown
tails and preserves the current default path. The existing resources setter
copies scalar values and known descriptor members, not the descriptor address;
failed validation leaves the configuration unchanged.

The corresponding normalized `session_allocation_resources_t` contains the
unchanged effective resource record, finite cap and a session-bound host service.
The complete existing prefix and normalized scalar policy, including C, are
identical for requirements and prepare. The new `allocation` member is NULL in
requirements: H+B is not finalized and no session-bound service exists yet. In
prepare it points to the initialized host service. This phase-specific borrowed
handle is part of the new tail contract, not a change to any existing resource
field; sizing cannot call or retain it. Providers copy retained service members/
scalars into their state, rather than retain callback-scoped resource/descriptor
pointers.
The copied service context remains valid through final backend destruction and
cleanup, including partial preparation. Raw caller callbacks are never exposed
to the backend. No session getter exposes those callbacks to ordinary consumers.

Existing named V1 sizes, offsets, trailing padding and prefix meanings remain
unchanged. Required semantics use the reserved resource bit behind that prefix,
not a new backend ABI. Old hosts reject the bit/mode before reading the tail;
old providers cannot advertise it and are refused before prepare. Explicit ABI 5
selection remains ABI 5 and is unsupported for elastic requests. Every path
requires both host and provider support. No fallback selects another backend,
drops a quota or changes `program_info` to make the request fit.

E/D/S/T and program/build bounds remain separate from the byte cap. Backend growth
cannot enlarge them. A production provider still declares its admitted program
and capacity combinations; the service alone promises no larger relation limit.

## 2. Caller allocator and exact accounting

The caller supplies synchronous `acquire(context, bytes, alignment)` and
infallible `release(context, pointer, bytes, alignment)`. The copied descriptor's
context and code must survive final release, including failed preparation.
There is no background operation or engine reentry. A session serializes all
service calls; different sessions sharing a caller context need a thread-safe
allocator or external serialization. One session cannot spend another's budget.

A backend payload request has nonzero bytes and power-of-two alignment no greater
than `alignof(max_align_t)`. Storage is writable, uninitialized, aligned,
exclusive and stable until release. It cannot overlap any live arena, input,
output or block. NULL transfers no ownership. Over-alignment, `realloc`, an
uncapped mode and direct heap/VM allocation bypassing the service are forbidden.
Native providers remain trusted code; this contract is not a sandbox.

The finite cap C bounds requested session execution-storage bytes:

```text
H = fixed host reservation, including metadata, scratch and result storage
B = assigned backend base arena bytes
A = sum of outstanding charged provider requests
R = H + B + A <= C
```

H+B equals the fixed plan's `total_execution_bytes`, including inter-slice
alignment padding: H here is not merely the plan's `host_bytes` member. Count
configured explanations and retained private policy snapshots in H, whether
their required slices are internal or externally supplied. Count each slice once;
unused caller buffer excess is excluded. Shared policy storage, compilation,
configuration handles, caller inputs, window adapter storage, stack and binding
objects are separate. C is not total application memory or RSS and excludes
allocator-internal overhead, which these callbacks cannot observe.

The service publishes immutable `tracking_bytes = K` and `maximum_alignment`.
K includes all per-block ownership/list metadata. Its exact charge is:

```text
charge(n, a) = K + (a - 1) + n
remaining = C - R
fits = charge(n, a) <= remaining
```

Both additions and H+B use checked `size_t` arithmetic. The host asks the caller
for exactly `charge(n,a)` bytes at `alignof(max_align_t)`, places metadata first
and aligns the payload after it. Padding actually unused remains charged. The
backend receives only the aligned n-byte payload; release recovers the original
provider pointer/charged size/alignment and returns that exact request once.
There is no separately growing host bookkeeping allocation. The implementation
must publish K for every supported ABI/build and prove metadata fits it; inspect
and acquisition use one checked calculation. No platform-independent K is claimed.

Admission requires H+B <= C. Preparation may add A after admission, without a
promise that the caller will satisfy it. Fixed plans keep A=0 and no service.
All private state, indexes, provenance, journals, provisional workspace and
simultaneous old/new blocks belong to B or A. Retained idle blocks count until
returned to the caller. Returning a slot to a pool does not reduce A. Growth is
in bounded blocks/pools, not per-fact or per-term requests; the provider publishes
its growth/retention policy. A finite cap is not justification for a historical
state leak.

### Inspect before choosing a request

`inspect(context, n, a, out)` returns C, R, remaining, operation peak, required
charge and `fits`. A valid request larger than remaining returns OK with
`fits=0`: inspection is not an attempted acquisition. Invalid shape/overflow
returns `INVALID_ARGUMENT` with unchanged output. Inspect never invokes the
caller, modifies storage/counters, poisons the transaction or changes an existing
sticky error. A backend can inspect a doubled block, inspect a smaller one and
choose before requesting either; it must not attempt and ignore a failed acquire.

Inspection is available during prepare/solve and permitted cleanup callbacks,
including after a sticky failure; it is unavailable in requirements, commit,
query or explanation callbacks. It describes only this serialized session at
the call. A later acquisition/release makes that snapshot stale. No other
session changes R, but it may consume resources of a shared caller allocator;
therefore `fits=1` never guarantees a non-NULL response or reserves anything.
Acquisition recalculates the charge/margin and validates its phase every time.
Its output block is cleared before other fallible work; no failure returns a
partially owned payload. The immutable service's maximum alignment is
`alignof(max_align_t)` for the loaded host ABI.

Successful acquisition updates current and peak after the caller succeeds. Cap
refusal never calls the caller; NULL never increases current. Attempt/failure
counters and operation peak are observational data. An operation begins with
peak=R, can raise it and preserves the final peak after success or rejection.
Abort restores current R and ownership exactly, not telemetry or the allocator's
internal heap state. No fit query can secretly increment an attempt counter.

## 3. Ownership, growth and strict abort

| Phase | Acquire | Release |
| --- | --- | --- |
| Requirements/configuration/admission | Never | Never |
| Elastic prepare | After cap admission | New provisional blocks only |
| Solve/candidate construction | Before any sticky failure | New provisional blocks only |
| Commit | Never | Never; install prevalidated ownership metadata |
| Abort: destroy_result without commit | Never | Every still-owned new provisional block |
| Accepted result cleanup: destroy_result after commit | Never | Superseded or temporary blocks with no remaining reference/lease |
| Explanations/query/enumeration | Never | Never |
| Session destruction/failed prepare cleanup | Never | All remaining owned blocks |

Successful preparation adopts its blocks when session initialization is accepted;
no solve/commit callback is synthesized for it. If preparation or a later
initialization check fails, failed-prepare destruction returns them all, including
when no backend state was returned. No session is published.

The ordinary growth sequence is acquire a new block during solve, construct a
candidate while preserving the old block, install the candidate at commit and
release the old block in accepted `destroy_result` after its last reference and
lease. No release occurs at commit. If old and new charges are O and N, both
belong to A before publication: the admission test includes R+N, not R-O+N.
Any copy/reset/release and service bookkeeping belongs to complete transaction
costs, including failed attempts. Commit must not discover an unreserved need.

An accepted operation may retain a bounded provisional block as future workspace;
it becomes preexisting, charged storage for the next transaction. Before that
acceptance it is provisional, even if the backend calls it a cache. On rejection
**all** new blocks are returned, every preexisting block remains owned, and the
same current charge is restored. Freeing and reacquiring an equivalent old block
is not rollback. There is no new-block cache after rejection in version 1.

Preexisting idle workspace can be reused without a callback, but its published
idle image and retained metadata must be restored. A provider using undo scratch
restores committed bytes, facts, identities, generations, free lists and indexes
exactly. The host checks acquisition-time ownership and phase; the provider owns
its transactional classification/restore protocol. Changes to caller allocator
internals and explicitly separate attempt telemetry are outside byte rollback.

The host validates all emitted output and acceptance conditions before one
infallible commit. Work/capacity limits, invalid emissions and an actual window
lease may reject after successful growth. Cleanup must return new blocks and
leave the prior result, explanations and references readable. An accepted
result's cleanup waits until all prepared explanations release their leases.
Destruction releases all remaining blocks once; valid cleanup never acquires or
fails. Invalid release pointers/double release/forbidden releases are native
provider contract violations, not recoverable allocator refusals.

### Logical identities outlive payload storage

Occupancy does not define addressable extent: a relation with one surviving tuple
can retain logical slot 255. A block strategy must preserve that identity without
reading outside its payload. This does not require a contiguous 256-row array;
indexed blocks or another checked mapping can preserve the same slots.

Generation history must survive payload deallocation. Required consumer evidence
retires a tuple, releases its block, reacquires at both a reused and a different
address, then reinserts it: the old identity stays invalid and the new generation
differs. Exhausted slots remain retired. Reinitializing a block cannot reset that
history. The base arena/other retained storage may hold the generation metadata;
all of it remains accounted. A fixed-arena slot witness alone does not establish
these deallocation properties.

## 4. Errors and execution identity

The backend returns the status supplied by a failed acquisition; the host also
preserves the first acquisition failure and refuses success even if the provider
ignores it. Further acquisitions cannot call the caller after that failure.
Inspect and cleanup cannot erase it. A normal NULL is an operation rejection,
not INTERNAL, termination, a partial result or permission for a smaller retry.
Correct cleanup leaves the session reusable.

| Failure | Existing status and diagnostic |
| --- | --- |
| Malformed descriptor/request, invalid alignment, checked arithmetic overflow | `INVALID_ARGUMENT`, before provider call; inspect leaves transaction/output unchanged. |
| Unsupported version/resource/mode/ABI or provider combination | `UNSUPPORTED`, before prepare. |
| Short supplied initialization arena | `STORAGE_TOO_SMALL`, required size/alignment, distinct from allocator refusal. |
| Exhausted admitted E/D/S/T | Existing `PAYLOAD_TOO_LARGE` with its dimension/bound. |
| H+B above C, or charge above C-R | `PAYLOAD_TOO_LARGE`, phase `backend-admit` or `backend-acquire`, field `execution_byte_cap`. No caller invocation. |
| Caller returns NULL for an admitted acquisition | `STORAGE_TOO_SMALL`, phase `backend-acquire`, field `allocator`. |
| Forbidden service phase, foreign-session use or violated lease | `INVALID_STATE`; violating an infallible callback/release remains a conformance failure. |

Service failures use diagnostic source SOLVE and preserve the existing versioned
diagnostic record with code NONE; no enum value is added here. Cap failure supplies CONTEXT and
CAPACITY, `observed_count=R+charge`, `limit=C`, `limit_kind=0` (this is not a build
limit). If that sum overflows, return `INVALID_ARGUMENT` instead. NULL supplies
CONTEXT, field `allocator`, and the charged request in `token` as unsigned
decimal; it must not invent a known storage-capacity shortfall. Bounded message
text may report current/cap/charge; the inspect record is the exact numeric
source, not prose parsing. Admission uses H+B as observed count. Diagnostic
validation itself precedes allocator invocation. No new numeric status or
machine-readable diagnostic section is needed for this proposal.

### Cap participates in the elastic fingerprint

The cap can change whether a transaction is accepted, so it is part of execution
identity. Preserve every existing FIXED fingerprint exactly. Let R2 be the
64-character lowercase SHA-256 from the existing
[V2 encoding](backend-session-resources-c-api.md#5-execution-identity-encoding-and-examples),
using normalized E/D/S/T, mode BACKEND_ELASTIC and the required allocator bit
(plus any other admitted required resources). The future host permits this only
after elastic admission; released hosts still reject it. The elastic identity
is SHA-256 of these ASCII bytes, including the final LF:

```text
maelys-execution-elastic-v1\n
R2\n
C\n
```

C is unsigned decimal without signs, spaces, leading zeros or NUL. R2 is ASCII
hex, not its binary digest. This new domain only applies to elastic mode; it
does not silently reinterpret or recompute existing fixed identities. Descriptor
size, allocation addresses/callbacks, K, current occupancy and operation peak
are excluded. They affect implementation/resource availability, not the declared
execution policy. Fingerprints do not guarantee allocator availability, equal
physical footprint or identical performance across builds.

Synthetic framing vectors, not execution evidence: for L = 64 ASCII zeroes,
E/D/S/T = 16/32/32/2048, mode=1 and required resources=3, the 103-byte V2 sequence
produces R2 = `d39ef61f5de8de1d50f94a3f7188c7bf239263bd6a848f1a8b2c59a97f52a014`.
Only the cap changes between these two 101-byte elastic sequences:

| C | Elastic SHA-256 |
| --- | --- |
| 1000000 | `ecdbd2975aa7afaf0b6aee802af34f84c7460539e2b5b019fba10f6a5131b729` |
| 1000001 | `d371145e31a9c60bdd605efb2b51988263a3df3a1b8855f5e514772e3c95bed6` |

## 5. Two windows, two independently charged sessions

Both adapters retain equal effective execution identities. In elastic mode that
requires equal normalized E/D/S/T, mode/features and configured C in both banks.
Each bank owns its cap and service: equal C is not a shared pool, a transferable
balance or half an implicit total. Their current/peak charges may differ during
alternation without changing identity. A mismatched cap is refused before any
provider preparation or publication. For adapter storage W,
report fixed reservation W+(H0+B0)+(H1+B1), current reservation W+R0+R1 and
configured bound W+C0+C1 separately. Never add H/B again to a cap that already
includes them; shared dependencies and other exclusions remain separate. A
shared caller allocator may fail while either bank is below its cap; this is
still recoverable NULL, not borrowing from the other bank.

Real last-N/group qualification must exercise alternation, duplicate facts with
several event/static supports, replacement, expiry and negation-induced growth.
Reject after growth via a live explanation lease and inspect both backend banks:
facts, text, identity/base, result, occurrence cursor and watermark remain as
before; new blocks disappear and old ownership/current charges match exactly.
An expiry with nothing due only advances time: no backend/service call or lease
change. An initialization probe abandoned before publication never commits state.
The modeled window in #140 is not evidence for these real adapters.

## 6. Implementation and independent consumer qualification

Only after this documentation is reviewed does a separate service implementation
begin. Keep fixed/default allocation and lease guarantees, installed-SDK ABI 5/6/7
consumers and language behavior unchanged. The host conformance provider can use
ABI 6 snapshot solve; there is no requirement to adopt ABI 7 to test allocation.

| Area | Required evidence |
| --- | --- |
| Declarations/admission | Separately compiled old/new callers, hosts and providers; short fields, full V1 and optional tails, untouched failed outputs, unknown versions/features, wrong mode and both independent opt-ins. Existing fixed readers refuse before callbacks. |
| Exact charge and inspection | Every supported alignment and charged boundary, zero/overflow inputs, C=H+B, one byte short/exact fit, fit followed by NULL, repeated oversized inspection then smaller successful acquire; no change to caller calls, attempts, peak or sticky state. Query and acquire share the checked formula. |
| Growth/peak | Old+new coexist through commit, cap where replacement alone fits but coexistence does not, never acquire/release in commit, old release only at cleanup, all metadata/padding/cached bytes counted. |
| Abort | Fail every acquisition ordinal in prepare/solve, including after earlier successes. Compare all committed payload bytes, preexisting ownership/current charge; every new block released once; retry succeeds. Peak/attempt telemetry retained separately. |
| Late rejection/leases | Swallowed refusal, emission/work/output validation and real window lease rejection after growth; no publication, intact old explanation/result, no early old-block release. |
| Two banks | Equal cap with unequal current charges, cap mismatch before prepare, independently exhausted caps, shared allocator refusal and alternating accepted bases. |
| Bounded reuse | Preexisting idle reuse, no new-block cache on abort, accepted workspace retention bounded by policy, long accept/reject/expiry traces, final destruction returns all blocks. |

A small native provider implementing a checked projection qualifies transport,
not a complete solver. It uses real provisional blocks and retained state, the
reference's complete oracle output, actual commit/abort and both adapters. Keep
host allocator traps active and count caller callbacks separately. No mocks may
erase output validation, partial preparation, deferred acceptance or leases.

Required named negative controls include wrong charge/padding, query side effect,
ignored cap, swallowed NULL, cache after abort, premature committed-block release,
missing cleanup, cap omitted from identity and publication after failure. A
passing baseline and a specific failed assertion are necessary; build/loader
errors are not mutant detections. Apply sanitizer-backed failure injection.

Then a reduced independent provider consumes the installed candidate SDK: move
only candidate relation storage and recursive workspace into blocks, keep the
existing logical limits and fixed production target, and exercise the slot-255,
retired-generation, recursive partial-frontier and real-window scenarios above.
Record immutable source/build/profile evidence and compare complete typed outputs
against the reference. This independent consumer must not define its own host
allocation ABI or import private implementation into the public repository.

Measure fixed reservation, current/peak charge, acquire/release counts, copied/
reset bytes and complete transaction Ir/Dr/Dw by function, including rejected
growth, cleanup and service accounting. Retain repeated counts and attribution
residuals. Reduced reservation or fewer allocator calls is not a speed claim.
Only implementation plus host and consumer qualification can advertise the
currently reserved feature; document any resulting contract correction before
freezing it. A qualified 0.x contract does not require a 1.0 release.
