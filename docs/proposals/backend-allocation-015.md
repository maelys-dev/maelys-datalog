# Optional backend allocation — planned additive 0.15.0 delivery

> Schedule update, 2026-09-30: v0.15.0 shipped the common JavaScript binding.
> Allocation remains deferred; the version references below record the earlier
> plan, not shipped support or a new release commitment. It is outside the three
> stages of [public input transactions](public-input-transactions-plan.md).

Status: deferred design, 2026-09-28. The accepted [0.14.0 capacity directive](backend-session-resources.md)
ships FIXED only. Its reserved allocator feature and BACKEND_ELASTIC mode are
always refused; no allocator callbacks, cap service or growth path ship in 0.14.0.

The service below is planned for 0.15.0 as an additive extension of the resource
record, using the stable prefix and required-feature mask. It must not change
ABI 6 callback signatures or the meaning/layout of an existing prefix. Older
hosts/providers reject its required bit before reading an unknown tail. Exact
allocator declarations, accounting charge and tests are reviewed in that delivery.
They are not blockers for 0.14.0 or its declaration review.

A production elastic backend remains a separate product qualification. The host
continues to reserve its own storage at creation; backend growth cannot increase
E/D/S/T or program/build limits. The following lifecycle/accounting design is
preserved from the initial proposal, not certified by its acceptance as a roadmap.

## 1. Allocator contract

### Provider and lifetime

The caller supplies one context and two synchronous callbacks, conceptually:

```text
acquire(context, bytes, alignment) -> pointer or NULL
release(context, pointer, bytes, alignment) -> void
```

The host copies the descriptor. Context and callback code remain valid until
session destruction and the final release have completed, including failed
preparation. No allocator operation escapes to a background thread or reenters
the engine. Sessions serialize callbacks; a context shared by different sessions
must support their actual concurrency or be externally serialized. It must not
use one session's service to allocate for another.

An acquisition requests nonzero bytes with a nonzero power-of-two alignment no
greater than `alignof(max_align_t)` in version 1. Returned storage is writable,
uninitialized, aligned, exclusive and stable until release; it must not alias a
live arena, input, output or another live block. NULL means ordinary acquisition
failure and transfers no ownership. A successful block is released exactly once
with its original provider pointer, requested byte count and alignment. Release
is infallible, bounded and requires no acquisition. There is no realloc service.
Over-aligned blocks and an uncapped mode are unsupported in this first extension.

The backend receives a session-bound host service, not the raw caller callbacks.
The service checks phase, arithmetic and the cap before calling the provider.
It records failures so a backend cannot swallow an acquisition rejection and
publish success. Direct heap/VM acquisition by this backend bypassing the service
violates the contract. Native providers are trusted code: enforcement of calls
through the service is not a sandbox against arbitrary native allocation.

### Accounting

The finite cap C bounds **requested session execution-storage bytes**, with:

```text
H = fixed host reservation, including host metadata, scratch and result storage
B = assigned backend base arena bytes
A = sum of outstanding provider request bytes, including service headers/padding
R = H + B + A <= C
```

H includes any retained private policy snapshot and configured explanation
reservation. Shared policy storage, policy compilation, configuration handles,
caller input buffers, window adapter storage, stack and Python/CFFI objects are
reported separately and excluded from this session cap. Excess caller buffer
bytes outside the assigned plan are also excluded. Do not label C as total
application memory, physical heap footprint or RSS. Provider-internal overhead
is not known from two callbacks and is not covered by this requested-byte bound.

All backend private retained state, provenance, indexes, journals, cached blocks,
candidate scratch and simultaneous old/new blocks belong to B or A. Retained
free blocks still count. Returning slots to an internal pool does not reduce A;
only returning a whole provider block does. Growth uses blocks/pools, not a
per-fact/per-term allocation scheme. The qualified backend publishes its initial
growth, block sizing and retention policy; an unbounded historical-state leak is
not permitted merely because a cap eventually stops it.

The host service owns counters and caps, including checked overflow; it rejects
before invoking `acquire` when the full charged request would exceed C. Metadata
needed to track blocks cannot require a separate growing host allocation: reserve
it in the plan or include it in the charged provider block. The implementation
must publish the exact charge function and boundary tests, including padding.
The backend still owns transactional block classification and rollback.

Admission requires H+B <= C. Successful preparation may additionally acquire
blocks, but all must fit C; the plan distinguishes fixed base bytes from observed
post-prepare reservation and does not promise that acquisition succeeds below
the cap. Fixed mode has no A or allocator service and reserves its complete plan.

Expose current R and operation peak separately from effective quotas and live
facts. Rejection restores ownership and current reservation; attempt counters,
failure reasons and peak telemetry remain separate observational data. Provider
internal heap state and external side effects are not byte-exact rollback state.

### Allowed phases

| Phase | Acquire | Release |
| --- | --- | --- |
| Requirements, configuration, admission | Never | Never |
| Elastic backend preparation | Allowed after cap admission | Provisional blocks only |
| Elastic solve/candidate construction | Allowed while no failure is sticky | Provisional blocks only; preserve every committed block needed for abort |
| Commit | Never | Never; install ownership using prevalidated metadata only |
| Abort (`destroy_result` without commit) | Never | Return every new provisional block |
| Accepted result release | Never | Unreferenced blocks; cached survivors still count |
| Explanation requirements/prepare/write, query/enumeration | Never | Never |
| Session destruction, including failed prepare | Never | Every remaining provider block |

The service remains available through cleanup but denies acquisitions once an
attempt fails. Allocator errors cannot be converted into a smaller accepted
result, hidden retry, alternate backend or implicit recomputation policy.
Explanation scratch is caller-owned or explicitly reserved; elastic storage does
not relax the existing explanation callback contract.

## 2. Transactions, errors and windows

During solve, previously committed backend state remains intact until acceptance.
New blocks are provisional. A provider may use reserved undo scratch but must
restore committed bytes, identities, free lists, ownership and retained-block
metadata exactly on rejection. It cannot free an old block before commit and
hope to reacquire an equivalent block during rollback. Scratch and attempt
diagnostics are explicitly distinguished from retained state.

The host validates the complete emitted result and all publication conditions
before one infallible commit. Result/explanation leases, window acceptance,
output validation and work-limit failure can still reject a candidate after
successful growth. Abandon it through `destroy_result`, reclaim provisional
blocks and preserve the prior accepted state. Commit cannot discover another
storage requirement. Result release waits for all prepared explanations; stable
blocks and references survive until their last lease ends.

Error mapping uses existing status values in this proposal:

| Failure | Status and diagnostic |
| --- | --- |
| Malformed configuration, invalid alignment or checked size overflow | `INVALID_ARGUMENT`, before mutation/provider call |
| Unsupported ABI/resource feature/mode or backend capacity combination | `UNSUPPORTED`, before prepare |
| Short supplied arena | `STORAGE_TOO_SMALL`, with required size/alignment |
| Exceeded admitted E/D/S/T or finite session cap | `PAYLOAD_TOO_LARGE`, with the effective dimension/bound |
| Provider returns NULL for an admitted acquisition | `STORAGE_TOO_SMALL`, phase `backend-acquire`, field `allocator`; distinguish it from a short initialization arena |
| Forbidden acquisition phase or violated lease | `INVALID_STATE`; a provider violating an infallible callback contract fails conformance |

For acquisition/cap failures, report requested charged bytes and current/cap
values where representable, using versioned diagnostic fields and bounded text.
Do not invent a build-limit selector for a session cap. Preserve the first sticky
failure even if the provider returns OK or emits further output; cleanup cannot
erase it. A normal NULL acquisition is an operation rejection, not INTERNAL,
process termination or permission to publish partial state. The session remains
usable after correct cleanup. Provider memory corruption or an OS-killed process
is outside that recoverability promise.

No new numeric status is required by this proposal. If implementation introduces
one, update callback validation, native mappings, CFFI/Python, diagnostics,
documentation and exhaustive-switch migration guidance together; unknown callback
statuses must still be rejected.

For standalone sessions, preserve existing input rejection semantics and retain
no borrowed callback inputs. For windows, preserve both bank/session contracts:
equal effective execution identities, separate disjoint storage and independent
allocator counters. Each bank has its own cap, not half of an implicit shared cap.
Total window planning reports adapter bytes plus both session reservations/caps
and separately retained dependencies. A shared caller provider may fail across
sessions even when each is below its own cap; that remains recoverable failure.

Test real last-N and group adapters with static replacement, deadline expiry,
negation-induced growth/failure and live explanations. Failure preserves visible
facts, result, text, occurrence cursor and watermark. An expiry with nothing due
advances time only: no backend call, allocation or result-lease change. An
initialization probe abandoned before publication commits no backend state.
This specification does not turn the modeled window in #140 into that evidence.

## 3. Qualification belonging to 0.15.0

These rows are deliberately outside section 9 of the 0.14.0 directive. Run them
against an installed SDK only when the service is implemented; inherited fixed
capacity, default, lease and ABI 5 tests must continue to pass.

| Area | Required evidence |
| --- | --- |
| Allocation admission/accounting | Trapping allocator proves no calls without explicit mode+feature; cap C at H+B and every charged boundary; header/padding overflow; two independent sessions sharing a provider; cached blocks and old+new peak counted. |
| Acquisition failures | Fail every acquisition ordinal in prepare and solve, including after successful blocks; compare complete committed state/ownership/current charge, release exactly once and retry successfully. |
| Late rejection after growth | Ignored acquisition failure, emission/work/output validation error or actual window lease rejection after growth; no commit, complete cleanup, readable old explanations and stable references. |
| Allocator lifetimes and phases | Copied descriptors, context through final cleanup, invalid phase traps, no acquisition during abort/commit/release/destruction, full reclamation or bounded cached retention. |

A small native ABI 6 provider implementing a checked projection qualifies the
transport. It uses real provisional blocks and retained state, returns the
reference's complete oracle result on admitted programs, and exercises actual
commit/abort and window interfaces. Do not stub away output validation, deferred
acceptance, preparation failure or result leases. Host allocator traps stay
active; caller-provider calls are counted separately.

Negative controls must detect missing opt-in, ignored cap, swallowed NULL,
leaked provisional blocks, premature committed-block release and publication on
abort. Use sanitizer-backed fixtures and a passing baseline; build/loader errors
are not mutant detections. Long reuse/expiry traces demonstrate bounded retention
and no leak proportional to cumulative events or vocabulary history.

Preserve the first failure, current/peak/attempt accounting, per-bank caps and
stable references. Publish the charge function including headers and padding.
Only then advertise support for the previously reserved feature. Validate the
older 0.14.0 reader's refusal with separately compiled consumers and providers.
The reserved bit does not itself certify this service or a production solver.
