# Session resource contract — design for the incremental backend

Status: design requirements, 2026-09-22. This document specifies the target
contract for bounded sessions. It does **not** describe an implemented API,
introduce new C declarations, raise current limits, or freeze backend ABI 4.
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
headers. Algorithm-specific data structures can remain private. Named use-case
profiles are versioned presets of explicit values, not separate dialects or
implicit backend selection.

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
   budget, explanation policy and permitted fallback behavior, fixed at creation.
3. **Occupancy and work consumed:** measured usage, separate from both limits
   and capability bits.

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

Accepting a plan guarantees storage for its declared capacities and enforcement
of the remaining ceilings. It does not prove that every input or every evaluation
schedule fits. Where static bounds cannot be established, runtime admission and
transaction failure enforce them. An implementation claiming acceptance based
only on final-state size must also prove that its intermediate/rollback needs fit.

## Admission and storage planning

The target lifecycle is:

1. Load and validate the program under supported structural bounds.
2. Select a backend and an explicit resource contract. Resolve defaults and
   profile versions before acceptance; inspect all effective values.
3. Check host/backend compatibility and compute a checked storage plan for that
   program, contract, backend and target build. Report bytes, alignments and
   persistent, transaction, result and explanation components, including their
   combined peak. Reject arithmetic overflow or unsupported combinations.
4. Initialize in caller-owned, aligned storage of the advertised size. A separate
   convenience constructor may allocate at creation, with its allocation count
   and capacity policy documented. Neither path grows during updates or falls
   back to heap allocation on exhaustion.

The session plan includes retained program copies and host reservations even
when an external backend is selected. Separately owned compilation and collector
storage are identified explicitly when calculating an application's combined
peak; a session-only figure is not a whole-process memory or allocation claim.

Planning is read-only and does not allocate an execution state. Initialization
checks the plan identity and storage ranges; it cannot silently reuse a stale
plan after a program, backend, capacity or build change. Validity and exclusive
ownership of supplied storage are checked before publication of a session.
Any backend unable to provide the bounded-storage guarantee must be rejected
for a request that requires it. Existing custom callbacks do not acquire this
guarantee merely by registering with the host; binding-side allocations remain
separate from engine allocation claims.

There is no silent capacity clamping. An unsupported request reports the failed
dimension and supported ceiling. Insufficient supplied storage reports its
required size/alignment. Runtime exhaustion reports the phase, dimension,
effective limit and observed or required usage when known. Failure to finish
analysis must not be converted into a negative logical answer.

Accepted capacities remain immutable until session destruction. Resizing is a
new admission/storage plan and a controlled rebuild in a new session, not an
implicit mutation of a live session's memory layout. Defaults preserve existing
behavior; new contract identities must not silently rewrite ABI 3 fingerprints.
A future execution fingerprint binds resolved capacities, work-accounting and
fallback policies, program/backend identities and profile version, not addresses
or current occupancy. Exact public encodings remain to be designed and tested.

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
output and explanation contract. A larger backend cannot promise fallback to a
reference build whose smaller limits reject that state. If no authorized path
fits, reject atomically; neither silently shrink the window nor summarize facts.

## Window and collector boundary

The engine accepts facts and transactions independently of Wasm, logs or kernel
traces. A collector/window adapter owns event decoding, occurrence IDs, explicit
expiration order and any mapping from one event to several facts. N events is
not necessarily N facts, nor a bound on derived facts. A retained dependency
outside the last N events needs a separate budget and property-preservation
contract; truncating it is not exact analysis of the original execution.

Window capacity, collection batch size and overflow/backpressure behavior must
match the accepted session contract. A rejected engine transaction must not make
the adapter advance its committed window. A shared bounded plan coordinates
collector and engine memory; it does not equate Wasm fuel, event counts and
Datalog work units or promise a wall-clock deadline.

## Validation and ABI gate

Before public API/ABI publication, validate both existing profiles and at least
two different session contracts in one process: boundaries, conflicting caps,
checked sizing, allocator-disabled updates, exact rollback/reuse, storage leases,
identity recycling and sustained windows. Generated small typed programs and
transaction sequences must be compared with full reference recomputation after
every commit, including negation, aggregate empty groups, removals/reinsertions,
failures and subsequent retries. Compare explanations and truncation when those
capabilities are promised; detect a backend that always falls back.

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
