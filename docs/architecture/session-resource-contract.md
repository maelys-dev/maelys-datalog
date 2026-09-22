# Session resource contract — design for the incremental backend

Status: design requirements, 2026-09-22. This document specifies the target
contract for session capacities and memory modes. It does **not** describe an
implemented API, introduce new C declarations, raise current limits, or freeze
backend ABI 4.
The current consumer API and backend ABI 3 retain their behavior and identities.
The [backend contract](compiler-backends.md) remains authoritative for that API.

## Scope and ownership

A resource contract belongs to the whole session: host input validation and
canonicalization, backend state, result publication, provenance and explanations.
A backend cannot accept capacities that the host cannot represent or publish.
The same Datalog program has the same complete result under every accepted
contract in which its execution succeeds. Exhausting a fact or transaction limit
is an explicit failure, never a successful partial result or a smaller implicit
window. Explanation limits remain distinct: a complete fact result may have a
bounded explanation explicitly marked as truncated under its existing contract.
An incomplete explanation must never be presented as exhaustive evidence.

Resource limits do not extend the source language or change aggregate semantics.
Public configuration and diagnostics must be usable without private backend
headers. Algorithm-specific data structures can remain private. The primary
consumer model is a catalogue of predefined, versioned profiles with explicit
capacities and storage guarantees. Consumers select a tested profile rather than
having to assemble independent limits. Profiles are not separate dialects or
implicit backend selection.

## Capacity profiles, memory modes and solver choice

The product target is the same language in a compact engine or in engines with
larger, potentially much larger, memory budgets. A full-language profile retains
typed values, stratified negation, bounded recursion and the four aggregates;
profiles change admitted sizes and resource budgets, not operator meanings.
The catalogue must declare capabilities as well as capacities. A restricted
experimental backend is not evidence that a full-language profile supports its
promised features.

Capacity and memory management are separate axes. A compact or very large
capacity profile can use fixed storage; an elastic session can start with a small
reservation. Neither axis selects the solver: reference and incremental backends
may support either mode. Each build must declare and validate its supported
profile/mode/backend combinations; this is not a promise that every combination
is implemented. Successful logical results agree on their common admitted domain.

| Memory contract | Reservation behavior | Allocation guarantee |
|---|---|---|
| Fixed, preallocated at creation | Finite reservation and peak bound; no growth during execution | No engine allocator calls on the configured execution path; creation/destruction may allocate/free |
| Fixed, strict native no-heap | Entire lifecycle uses caller-owned, bounded storage | No heap allocation or deallocation from initialization through destruction, including errors and explanations, within the audited native component |
| Elastic, explicitly selected | Blocks/pools can grow and be reclaimed under the accepted policy | Allocations are allowed; a configured memory cap is enforced, and allocation failure rejects the operation atomically |

These are design targets. The current reference implementation guarantees zero
engine allocator calls on its covered execution path **after session creation**;
it does not guarantee a heap-free lifecycle. Current ABI 3 policy/session setup,
legacy explanation wrappers and binding conversions may allocate. Custom backends,
callbacks and allocations internal to external libraries are not automatically
covered by that existing guarantee. Neither strict native no-heap sessions nor
elastic sessions are implemented by this document.

## Fixed storage and strict native no-heap

Every fixed combination specifies a finite reservation/peak bound for its target
build, backend and explanation mode. It covers host/backend persistent state,
result/provenance, indexes, transaction rollback, scratch and bounded automatic
stack usage. Live data can vary inside the reservation; the reservation cannot
grow. Exhaustion fails explicitly, even for the largest profile. There is no
automatic transition to elastic storage or fallback heap allocation.

Two provisioning paths preserve the fixed execution guarantee:

- **Caller-owned static storage:** the application may reserve an aligned arena
  at build time. The selected SDK/profile must publish a checked storage upper
  bound and alignment for that target and supported mode, valid for admitted
  programs. Initialization checks the actual plan against the supplied arena.
  A runtime-only size query is insufficient for this provisioning path. Native
  structure layouts remain private; exact sizing metadata/API is not frozen here.
- **Preallocation at session creation:** a convenience constructor reserves the
  same bounded storage, states its allocation count and never resizes it during
  execution. This is fixed-capacity execution, not a claim that initialization
  itself avoids the heap.

The fixed execution path requires zero engine allocator calls during input updates,
solve, queries, result release and explanation preparation/writing in configured
or caller-owned storage. Legacy allocating explanation convenience calls are
outside that path. A creation-time allocation followed by fixed execution does
not establish the stronger strict native no-heap guarantee.

The strict variant requires caller-owned storage for the program, configuration,
session, facts/text, indexes, supports, transaction state, results, provenance and
all explanation scratch. Its guarantee starts with planning/loading and
initialization and continues through every operation and destruction, including
validation, diagnostics, saturation and other error paths. Storage is returned to
the caller without invoking a heap deallocator. Source compilation either occurs
offline, outside the deployed component, or uses a separately validated bounded
compiler workspace supplied by the caller. Offline compilation does not excuse
allocations in the deployed loader, compatibility checks or program validation;
it also does not define a new serialized program ABI here.

The strict build must exclude allocating convenience entry points and audit the
transitive dependencies, libc calls, callbacks and backend used by that artifact.
No reachable path may call `malloc`, `calloc`, `realloc`, `aligned_alloc`, `free`
or equivalent allocation/deallocation services, directly or indirectly. Merely
renaming an allocator, delegating it to a callback or using growing virtual-memory
mappings does not satisfy this contract. Unbounded stack/variable-length arrays
are not substitutes for a heap budget. Only validated providers can participate;
reject an unsupported combination before publishing a session.

Build/link symbol checks and dependency review must be combined with allocator
traps and failure-path tests across the full lifecycle. Symbol checks alone can
miss indirect/internal calls; tests alone do not audit unexecuted dependencies.
The guarantee applies to the identified native build, target and provider set,
not to Python/CFFI conversions, the caller's whole process or an unaudited runtime.
Wasm host and linear-memory growth need their own explicit contract.

## Elastic storage

Elastic storage is an explicit alternative, with no per-fact/per-term allocation
scheme: grow through blocks or pools. The accepted policy specifies initial
reservation, growth granularity, reclamation, retained free-block limits and the
memory cap, or an explicit absence of an application cap. A cap covers combined
host/backend reservations, metadata, cached blocks and transaction peaks,
including staged growth and any simultaneous old/new storage. Specify the byte
accounting basis: requested engine storage is not automatically an allocator's
actual footprint or process RSS; padding and provider overhead must be bounded
and included before advertising a bound on the complete allocation footprint.

Without an application cap there is no promised total byte bound known in
advance. Representation ceilings, checked arithmetic, admitted relation limits
and work limits still apply. Accepting an elastic contract does not guarantee
that future allocations will succeed. A cap is a maximum, not reserved memory.
Growth changes actual reservation within accepted ceilings; it never silently
raises per-predicate limits or widens an incompatible representation.

Growth must preserve all live identities and references in facts, indexes,
supports, proofs and results. Stable blocks or validated handles/indirection are
possible strategies; relocating storage without repairing every reference is
not. Allocation failure reported by the allocator rejects the whole operation
without publishing a candidate state. This is not recovery from an OS process
termination. New blocks remain provisional until commit and are released on
failure; rollback cannot require another allocation. Previously committed
storage and persistent allocator bookkeeping are restored, including free lists
and retained-block ownership. Attempt diagnostics remain separate.

Expiration reclaims unreferenced entries. Empty blocks may be returned or retained
for reuse only under the explicit retention policy; retained blocks still count
toward the cap. Reclamation may not invalidate live references, and an unbounded
history of expired events must not become retained maintenance garbage. Allocation,
reclamation and rollback costs belong in end-to-end measurements.

## Catalogue and representation

An embedded build may include only its chosen profile and representation. A
larger build may offer several validated profiles, each with its own reservation.
Choosing a compact profile must not reserve the largest profile's arrays. Wider
indices required by a large profile must not silently enlarge the compact
representation. Selecting a larger build does not by itself establish that
all larger capacities, intermediate states, stack bounds or work budgets are safe.

Profile names, byte sizes and capacities require measurements and conformance
evidence before publication; this design does not invent an XL profile by
multiplying today's macros. Existing SMALL/LARGE behavior stays unchanged.
Advanced custom contracts may be considered separately; the initial product
does not require arbitrary runtime tuning of every bound. Changing accepted
capacities or memory mode requires new initialization and controlled state
reconstruction. Elastic growth/reclamation inside the accepted contract is not
a profile change.

## Current boundary

At engine commit `7ab0a2b`, capacities come predominantly from the loaded build:

| Capacity | SMALL | LARGE |
|---|---:|---:|
| Dynamic EDB facts | 1,024 | 2,048 |
| Derived IDB facts | 1,024 | 2,048 |
| Facts per predicate | 64 | 256 |
| Symbols | 512 | 512 |
| Symbol text bytes | 32,768 | 32,768 |
| Resolution depth | 10 | 10 |

These are independent constraints. Program shape also has fixed limits, including
arity, predicates, rules and strata. Raising fact capacity does not raise depth,
proof capacity or index width. Several internal arrays use the build constants
directly and several counters/indices use `uint16_t`.

`maelys_datalog_limit_get` reports build capacities, not per-session quotas.
The public input buffer already supports smaller capacities and caller-owned
storage, but that does not resize the prepared session or result arrays.
`backend_emit` still enforces host total and per-predicate IDB limits, including
non-query helpers. Every ABI 3 backend receives input after host materialization.

## Three distinct quantities

1. **Implementation ceilings:** what the compiler, host representation and
   selected backend support. Program admission limits are checked before a
   session exists; a session cannot rescue a rejected program.
2. **Session contract:** the requested live capacities, batch limits, work
   budget, explanation policy, memory mode/cap/growth/retention policy and permitted
   fallback behavior, fixed at creation.
3. **Usage:** live occupancy, current and peak reservation, and work consumed,
   separate from contract limits and capability bits. Occupancy and reservation
   are not interchangeable, especially when elastic pools retain free blocks.

A smaller quota on a fixed array does not save the array's memory. The first
implementation may enforce quotas below current ceilings, but must report its
actual reservation. Supporting independently sized sessions or larger capacities
requires capacity-sized storage throughout the affected host/backend path.
Structural language/IR bounds need not all become runtime parameters.

## Resource dimensions

| Resource | Required meaning |
|---|---|
| Live EDB | Total distinct dynamic facts and caps per declared EDB predicate. Policy facts remain part of the admitted program. |
| Live IDB | Total distinct derived facts and caps per declared IDB predicate, including helpers. |
| Update batch | Raw added/removed entry counts and bytes before normalization; duplicate updates still cost validation and storage. |
| Vocabulary | Live program/input symbols, maximum string size and total retained text bytes, including retained-state references. |
| Maintenance | Bounded indexes, support information, aggregate groups/value multiplicities, invalidation queues and deltas. Backend reports its storage needs and supported ceilings. |
| Transaction peak | All storage simultaneously needed to preserve the old state, build the candidate state and undo a rejected update. |
| Results and explanations | Snapshot/provenance reservation and opt-in explanation preparation/scratch, with separate completeness/truncation semantics. |
| Work | One attempt budget with a named, versioned accounting model; includes validation/normalization, maintenance, publication and any permitted fallback. |

Per-predicate caps do not reserve their sum implicitly: the global cap also
applies, and the storage plan states its actual allocation strategy. Bounds on
facts do not bound the number of supporting rule instantiations. Support counters
need justified widths and checked overflow; saturation followed by decrement is
not a correct deletion algorithm.

The work-accounting model specifies how host and backend operations charge the
same attempt budget, including their units and granularity. It is not a portable
conversion to CPU instructions, time or Wasm fuel. An unsupported accounting
model cannot be accepted as an enforced work limit.

Successful fixed-storage initialization reserves the plan's declared storage;
elastic initialization reserves only its initial allocation. Both enforce all
accepted ceilings. Neither proves that every input or evaluation schedule fits
the fact, work or transaction limits; the elastic case can additionally encounter
allocation failure within its cap. Where static bounds cannot be established,
runtime admission and transaction failure enforce them. An implementation claiming
acceptance based only on final-state size must also prove its intermediate/rollback
needs fit.

## Admission and storage planning

The target lifecycle is:

1. Load and validate the program under supported structural bounds.
2. Select a backend, predefined capacity profile and memory mode. Resolve defaults,
   profile versions and all resource policies before acceptance; inspect effective
   values and reject unsupported combinations.
3. Check host/backend compatibility and compute a checked storage plan for that
   program, contract, backend and target build. Report bytes, alignments and
   persistent, transaction, result and explanation components, including their
   combined peak for fixed modes and capped elastic storage. Distinguish elastic
   initial reservation from its maximum; mark an uncapped total explicitly.
   Reject arithmetic overflow or unsupported combinations.
4. Initialize fixed storage in a supplied aligned arena or through the separately
   documented preallocating constructor. Strict no-heap permits only the supplied
   path. Elastic initialization uses its declared initial reservation and provider;
   growth is allowed only in that mode and under its accepted policy.

The session plan includes retained program copies and host reservations even
when an external backend is selected. Separately owned compilation and collector
storage are identified explicitly when calculating an application's combined
peak; a session-only figure is not a whole-process memory or allocation claim.

Planning is read-only and does not allocate an execution state; strict no-heap
planning itself must use bounded caller-owned or automatic storage. Initialization
checks the plan identity and storage ranges; it cannot silently reuse a stale
plan after a program, backend, capacity, memory policy or build change. Validity
and exclusive ownership of supplied storage are checked before publication of a
session.
Any backend unable to provide the bounded-storage guarantee must be rejected
for a request that requires it. Existing custom callbacks do not acquire this
guarantee merely by registering with the host; binding-side allocations remain
separate from engine allocation claims.

There is no silent capacity clamping. An unsupported request reports the failed
dimension and supported ceiling. Insufficient supplied storage reports its
required size/alignment. Runtime exhaustion reports the phase, dimension,
effective limit and observed or required usage when known. Failure to finish
analysis must not be converted into a negative logical answer.

Accepted capacities and memory policies remain immutable until session destruction.
Changing them requires a new admission/storage plan and controlled rebuild; elastic
reservation growth/reclamation within them does not. Defaults preserve existing
behavior; new contract identities must not silently rewrite ABI 3 fingerprints.
A future execution fingerprint binds resolved capacities, work-accounting and
fallback policies, memory mode/initial reservation/cap/growth/retention policy,
program/backend identities and profile version, not addresses or current
occupancy/reservation. An absent cap has an explicit meaning, not an ambiguous
zero. Exact public encodings remain to be designed and tested.

## Incremental transactions

The target update is set-based: `next_EDB = (EDB minus removed) union added`.
Duplicate entries are idempotent after validation; removing an absent fact is a
no-op; a fact present in both lists is present in the next EDB. Raw batch limits
and validation still apply to all submitted entries, including cancelling ones.
Distinct stream occurrences need distinct event identities.

The initial full snapshot and subsequent delta updates publish the complete IDB.
An update is accepted only without a live result lease. Result IDs and retained
explanations remain stable throughout their lease. Private identities may differ
from each snapshot's canonical IDs; recycling is forbidden while **any** live
fact, index, support, provenance or result still references the identity.
Maintenance garbage must be reclaimed within declared bounds over a long stream,
not accumulate with the total number of events ever observed.

Capacity checks concern the final live relation **and** the intermediate peak.
Replacement at a full window is allowed when the plan can represent the final
state and its transaction scratch; raw additions must not require an extra live
slot merely because the removal is processed later. An algorithm may reject when
its declared transaction workspace is insufficient, even if final live counts fit.

On failure, persistent facts, identities, indexes, support counts, free lists and
preflight bookkeeping are restored byte-for-byte. Scratch and attempt diagnostics
are explicitly separate from persistent state. Attempt work is reported even on
failure; it is not erased by rollback. The previous committed state remains
usable. No candidate result becomes observable before the complete update commits.
In elastic mode this includes provisional blocks and pool metadata as specified
above; allocator-internal process state is not engine-owned rollback state.
In fixed mode, incremental maintenance takes and returns slots inside reserved
pools without requesting more memory. The same transaction semantics apply in
both modes; allocation strategy does not alter negation or aggregate semantics.

Even insertion-only EDB updates can delete IDB facts through stratified negation
or replacement of aggregate outputs. A positive-only prototype must reject these
programs or use an explicitly permitted full-recompute path; it must not maintain
them as if they were monotone. Aggregates keep their existing snapshot semantics:
replacement removes the old tuple and inserts the new one atomically.

## Observable, bounded fallback

Fallback is part of the accepted contract, not a silent algorithm substitution.
A backend may maintain some strata and recompute affected strata above them, but
must propagate every positive, negative and aggregate dependency and publish the
whole transaction atomically. Stratified rules consume the appropriate completed
lower-stratum state; no mixed-generation snapshot may escape.

An attempt report records maintained/recomputed strata, the reason for fallback,
work already spent and work spent in recomputation, and the next maintenance
mode. A differential test asserts this path as well as the resulting facts.
Recomputation and rebuilding maintenance state share the original attempt budget.
If only a recompute-only mode fits, it must have been permitted and be reported.
That mode cannot later claim incremental maintenance until state reconstruction
succeeds within the same resource discipline.

The fallback implementation must support the same admitted program, capacities,
memory mode, output and explanation contract. Strict no-heap cannot fall back to
an allocating solver; fixed storage cannot become elastic on exhaustion. A larger
backend cannot promise fallback to a reference build whose smaller limits reject
that state. If no authorized path fits, reject atomically; neither silently
shrink the window nor summarize facts.

## Window and collector boundary

The engine accepts facts and transactions independently of Wasm, logs or kernel
traces. A collector/window adapter owns event decoding, occurrence IDs, explicit
expiration order and any mapping from one event to several facts. N events is
not necessarily N facts, nor a bound on derived facts. A retained dependency
outside the last N events needs a separate budget and property-preservation
contract; truncating it is not exact analysis of the original execution.

Window capacity, collection batch size and overflow/backpressure behavior must
match the accepted session contract. A rejected engine transaction must not make
the adapter advance its committed window. A shared resource plan coordinates
collector and engine memory; a combined finite-memory claim requires both sides
to provide compatible finite bounds. It does not equate Wasm fuel, event counts
and Datalog work units or promise a wall-clock deadline.

## Validation and ABI gate

Before public API/ABI publication, validate both existing profiles and each new
predefined profile/memory-mode/backend combination being offered. If a build
offers multiple profiles, validate at least two different session contracts in
one process: boundaries, conflicting caps, checked sizing, allocator-disabled
updates, exact rollback/reuse, storage leases,
identity recycling and sustained windows. Generated small typed programs and
transaction sequences must be compared with full reference recomputation after
every commit, including negation, aggregate empty groups, removals/reinsertions,
failures and subsequent retries. Compare explanations and truncation when those
capabilities are promised; detect a backend that always falls back.

Additional memory-mode evidence is required before claiming each guarantee:

| Contract | Required evidence |
|---|---|
| Fixed execution | Allocator counters and disabled-allocator execution after creation, including updates, solve/query, result release and configured explanations; capacity failures and reuse; no elastic fallback. |
| Strict native no-heap | Checked static arena bounds/alignment and stack bounds; artifact/dependency audit, allocating entry-point exclusion and direct/indirect allocator traps from planning/loading through destruction; malformed programs, saturation, explanations, teardown and retries with every heap service forbidden. |
| Elastic | Failure injection at every growth point, cap boundaries including staged/cached blocks, stable references across growth, exact persistent rollback without allocating, and successful retry; sustained expiration/reuse with observed block reclamation/retention and no historical-state leak. |
| Cross-mode | Complete canonical results and promised explanations agree for the same admitted program/state; unsupported combinations reject; compact builds do not inherit large-profile representations or elastic-only state. |

The existing engine allocation guard is evidence for the execution paths and
allocator entry points it instruments, not certification of a strict artifact or
its transitive libraries. Distinguish tests already present from this future
conformance matrix; no new mode is certified by documenting its requirements.

ABI 3 can host a bounded private experiment receiving full snapshots and diffing
them. It remains subject to the current host materialization and output limits;
this does not implement per-session capacity negotiation or a public delta API.
The first experiment must nevertheless account for all storage and update peaks.

Freeze ABI 4 callbacks and compatibility rules only with the validated backend.
The opaque consumer configuration, context/binding selection, capability checks,
storage planning and effective-limit introspection must agree across that boundary.
Keep Wasm fuel and event counts separate from the agreed Datalog work model;
native callbacks remain trusted code, not a memory or CPU sandbox.

Performance acceptance measures initial solve and steady-state transactions end
to end, including input work, maintenance, canonicalization, publication,
explanations when used, release, fallback and peak memory. A small delta, a low
impact ratio or configured capacity alone establishes no speedup.
