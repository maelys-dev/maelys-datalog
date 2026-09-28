# Backend transaction deltas — engine response

Status: proposal for agreement, 2026-09-28. Engine baseline: `af38f2c`.
Input reviewed: the downstream written proposal `backend-transaction-deltas.md`
dated 2026-09-27, revision `73f349213197129d8e44bd6967100284fd0e7d9d`.
Only that proposal was consumed; no downstream implementation is imported.

The engine agrees that a native delta path must avoid compulsory reconstruction
and validation of the entire input before the backend callback. A callback that
receives deltas only after that reconstruction would not meet the stated need.
Agreement on this note precedes any implementation experiment or public API
design. No signature, capability bit, descriptor layout or ABI number is
reserved here. Existing snapshot consumers continue under backend ABI 5; their
migration is not a prerequisite for the downstream snapshot implementation.

The [current backend contract](../../include/maelys/datalog_backend.h),
[session resource proposal](../architecture/session-resource-contract.md) and
[window transactions](../architecture/window-transactions.md) are the starting
points. The guarantees below are proposed acceptance conditions, not claims
about a delta API already supplied by the SDK.

## What the existing host does

[`session_solve`](../../src/runtime/maelys_datalog_runtime.c) validates a complete
input batch, calls the prepared-session materializer, exports canonical facts
for an external backend, then validates and publishes its complete emitted IDB.
The [materializer](../../src/core/maelys_datalog_prepared_session.c) restores the
program dictionary, collects and sorts input symbols, inserts facts and finalizes
the EDB. Retaining backend state does not remove these host operations.

The reference can borrow materialized inputs; an external ABI 5 backend receives
the complete canonical input array. Queries and explanations also depend on the
host's retained EDB and vocabulary. Removing snapshot delivery therefore does
not permit the host to discard its logical EDB or weaken result completeness.

The consumer's reported exclusive instruction counts identify repeated host
work, but are neither the whole host cost nor savings attributable to a delta
contract. Some named functions also serve other paths. Those measurements were
not replayed for this note, and no delta-versus-snapshot gain has been measured.

## Guarantees proposed for agreement

### State identity and set semantics

Initialization establishes a complete committed EDB. Replacement is an explicit
transaction, including replacement by the empty set; it is not an implicit
response to a stale delta. A delta names its owning session incarnation and
committed base generation. Another session, even with the same program and
numeric generation, is not that base. Session reset/recreation cannot resurrect
an old token. Exhaustion is rejected before mutation; counters never wrap into
an accepted old identity.

For ordinary dynamic input, the transition is
`D_next = (D_committed minus removed) union added`. Additions win when a fact is
in both lists, regardless of list order. Duplicate additions and absent removals
are no-ops after validation. No intermediate absence is visible to negation,
aggregates, queries or the backend. Raw batch limits and type/schema validation
apply even to duplicate or cancelling entries; final occupancy and staging peak
are checked separately. Rejecting an invalid entry cannot be avoided by pairing
it with its inverse.

Each accepted delta transaction advances its generation once, including a
validated set no-op; a rejected attempt leaves it unchanged. That generation is
distinct from a result-cache generation, event cursor or expiry watermark. The
existing window expiry with nothing due remains a watermark-only operation:
it initiates no backend transaction and preserves its current result lease.

The host preserves predicate identity, arity, value kinds and current boolean
normalization. A symbol spelling an integer or boolean is still a symbol.
Canonical public result IDs and enumeration must match a fresh snapshot of the
same final input and program, independently of transaction history. IDs remain
stable throughout a result lease, not across arbitrary generations. Persistent
backend keys cannot be raw IDs borrowed from a previous public result.

### Origins, occurrences and the two window sessions

Program ground facts remain immutable program data, outside the dynamic removal
set. Existing predicate-role admission remains in force: a delta is not a way
to submit runtime input to a policy-fact predicate. Changing program ground
facts requires a different loaded policy, not a dynamic delete.

Window static input is a different origin from program ground facts. The host
owns its explicit replacement and the ledger of event/group contributions.
For a fact `f`, window input membership is the union of its static and retained
event suppliers. An expiration removes `f` from the backend EDB only when its
last supplier disappears. Group membership, occurrence counts, EDB membership
and rule-derivation supports are four separate quantities. A change of supplier
with unchanged union produces no backend membership delta. Occurrence metadata,
raw capacity accounting, cursor and watermark still commit atomically.

The current adapters alternate between **two separately prepared sessions**.
The inactive backend can therefore describe an older EDB than the window's
visible state. Sending the latest window delta directly to it is incorrect.
The first window experiment must track each bank's own base identity and compose
the transition from that bank's committed EDB to the candidate union, using
bounded host-owned history or membership state. The external window generation
and the physical bank's base must both be checked. Initial probes that are
aborted establish no committed backend base.

No opaque backend arena may be copied to synchronize the banks. Required
catch-up, composition, initialization and any explicitly selected full-snapshot
resynchronization must appear in storage and work accounting. A resynchronization
is an observed snapshot path, never relabelled as native delta execution. This
bank protocol must be demonstrated before claiming window delta support; a
single-session experiment does not establish it. Replacing the two-bank model
would need a separate lifetime review, not a relaxation of the existing lease.

### Vocabulary and storage

The first bounded experiment should use a declared fixed vocabulary and integer
occurrence IDs. A new symbol in an addition is rejected before persistent
interning if it is outside that vocabulary. An absent removal by otherwise valid
typed value does not intern text merely to discover absence. A fixed allowed
vocabulary is an admission constraint, not permission to expose unused entries
as additional public canonical symbols.

This restriction is explicit experimental scope. Existing snapshot APIs continue
to accept bounded runtime strings under their current contract. A later mode
that admits vocabulary changes must atomically maintain text, references and
reclamation across live facts, indexes, journals, proofs and results. It must
survive more cumulative strings than fit simultaneously. An indefinitely
append-only dictionary is not that mode. Neither mode may retain ephemeral
caller strings or claim that a symbol number is a global stable key.

Caller input descriptors/text are borrowed only during the synchronous operation;
anything needed for a pending candidate, commit or explanation is copied or
interned into reserved storage before that borrow ends. Backend-visible staged
deltas remain valid through acceptance/abort. Afterward, retained backend state
must use owned storage or explicitly leased committed storage, not a staging
pointer that the next transaction will overwrite.

Preparation must report checked bytes and alignment for host persistent state,
raw batch capacity, distinct facts, vocabulary/text, index/support state,
rollback, both window banks, result/provenance and configured explanations.
Backend requirements remain a separate contribution to this plan. Caller-owned
storage and an allocating convenience constructor have distinct lifecycle
guarantees. Fixed execution has no growth, per-fact allocation or heap fallback.
All journals, abort obligations and final-state/peak capacities must fit before
publication; replacing a fact at full occupancy need not require an extra live
slot if the declared staging plan can represent the transaction.

Session quotas must not rewrite `program_info` or its program/build limits.
ABI 5 storage preparation alone does not provide capacity-sized host storage or
whole-host work accounting. Those remain explicit future admission contracts.
Canonical remapping, complete IDB publication and snapshot-only consumers can
still require work proportional to live state: no whole-request O(delta) promise
follows from incremental input maintenance.

### Atomic acceptance, rollback and work

Validate/admit, stage, evaluate and validate complete output before publication.
At the last fallible boundary, verify result/explanation leases and every
reservation. Only then publish host input, backend state, result and window
metadata together, with one infallible commit. Commit cannot allocate, reenter
or discover insufficient capacity/work. Abort remains bounded and allocation
free. Reserve its cleanup obligations before provisional mutation, and commit
work before crossing the publication boundary. Distinguish reserved work from
work actually consumed; rejected attempts still report the latter.

Byte-exact rollback covers all **retained** host/backend state: facts, dictionary
entries/text, registry/index data, support counts, free lists, persistent
preflight bookkeeping, generations and committed window metadata. A successful
backend evaluation followed by a host emission error or late lease rejection
still aborts. Output error latching cannot be bypassed by a backend returning OK.
There is no new result or partially advanced cursor/watermark on rejection.

The request to restore all "preparatory state" needs this qualification:
explicitly designated candidate scratch and attempt diagnostics may change.
They must contain no surviving borrow and be safe for the next attempt. This
matches the existing window contract, whose inactive bank can change on failure.
The storage plan must name these ranges before tests run; a persistent index or
free list cannot be retrospectively called scratch to excuse failed rollback.
Attempt work/diagnostics are outside the restored state so rejection does not
erase consumed work. No guarantee covers arbitrary external callback side
effects or recovery from process termination.

One published result leases its session; direct mutation while that result or
an explanation is alive is rejected. A window may stage in its other session,
but cannot publish until the old result is releasable. Retained proof inputs
must stay readable on a late rejection. Releasing a result is not rolling back
a committed EDB. Callbacks remain trusted, session-confined and non-reentrant;
cooperative work units are neither a sandbox nor a wall-clock deadline.

## Refusals and compatibility boundary

The proposed contract refuses stale/cross-session bases, unsupported language or
operation modes, malformed types/arities/predicate roles, invalid lifetimes,
exhausted generations and any exceeded declared capacity or work budget before
publication. Diagnostics must distinguish raw batch, unique/per-predicate facts,
vocabulary/text, backend state, staging and work failures, including the failing
phase and consumed attempt work. This note does not assign new status values;
any necessary extension requires its own header/binding/diagnostic review.

Unsupported native delta support must not silently select another algorithm.
A caller may explicitly select a snapshot adapter, with its reconstruction
visible in identity/diagnostics and measurements. Existing ABI 5 providers keep
their snapshot contract. Exact-size/version descriptor validation cannot be
bypassed by appending a guessed callback or reinterpreting a capability bit.
Any future negotiation must specify compatibility and descriptor traversal;
the versioning choice comes after semantic agreement.

No success may mean a partial IDB, suppressed helper facts or skipped negation.
Removing EDB can enable derivations; inserting EDB can retract conclusions via
negation or aggregates. Recursion, aggregates and explanations retain their
existing semantics on the admitted language subset. An unsupported subset is
rejected or uses an explicitly admitted, observable recompute path. Delta IDB
output, query-local evaluation and vocabulary reclamation are not implicitly
bundled into this first input-delta experiment.

## Oracle and conformance experiment

Use the **same installed SDK revision, profile and program** for both paths.
An independent test model applies typed set updates and, for windows, a separate
origin/occurrence ledger. From that model construct a fresh complete snapshot
and solve it with the reference after every accepted transaction. Do not derive
the oracle snapshot from the candidate's own indexes or delta-normalization
code. Cross-version results are not the semantic oracle.

Compare complete query-visible facts and derived-IDB enumeration, including
helpers, kinds, values, canonical IDs, order and symbol resolution. For promised
explanation kinds, compare structured premises/support validity and bounded
truncation against the reference; equivalent alternate proofs need not have
identical formatting unless that exact format is promised. Exercise live
prepared explanations, caller input clear/reuse and both release orders.

Deterministic and seeded generated sequences must cover:

- empty deltas, repeated additions, absent removals, conflicts in both lists,
  type distinctions, boolean normalization, permuted lists and replacement at
  fixed cardinality; invalid cancelling entries still reject;
- at least two session incarnations and several generations, stale batches,
  reset/recreation, token exhaustion, initialization failure and explicit replace;
- exact capacity and one beyond for each independently bounded resource,
  index collisions/wrap, symbol rejection, partial staging and successful retry;
- static/event duplicate suppliers, multiple groups sharing facts, first versus
  last supplier expiry, equal deadlines, expiry without insertion, watermark-only
  no-op, exhausted occurrence IDs, and each alternating bank after aborted probes
  or repeated failures;
- recursive orphan derivations, alternative remaining supports, negation and
  aggregate changes on insertion/removal, including a removal that overflows IDB;
- callback/emit/filter/work failure and late explanation-lease rejection after
  evaluation, followed by reuse and the same successful snapshot result.

At each failure compare full declared persistent arenas (including inactive
tails and bookkeeping) byte for byte with their pre-attempt images, and compare
all previously borrowed committed views. Check diagnostics/work separately and
poison permitted scratch before reuse. No-op expiry must keep its result pointer
and make no backend call. Guard allocations across host, adapter, backend,
query, configured explanations, commit/abort and result release in fixed mode;
Python/CFFI conversions remain outside a native zero-allocation claim.

Mutation checks must detect premature deletion/publication, first-supplier
expiry treated as last, wrong-bank or stale-base acceptance, erased value kinds,
late capacity checks, incomplete symbol/index rollback, lost work on rejection,
fallible commit, stale borrowed text, incomplete IDB and an unreported snapshot
fallback. Capability declarations alone are not these proofs.

## Delta-versus-snapshot measurement of host savings

After agreement, implement an experiment confined to the engine's test driver,
with no installed callback or ABI commitment. Predeclare three paths:

| Path | Host input path | Backend delivery |
|---|---|---|
| A: current snapshot | Current full-batch validation/materialization/export | Unchanged ABI 5 snapshot provider |
| B: delta-to-snapshot | Persistent host EDB, changed-input validation, staged delta and rollback; then complete canonical snapshot export | The **same** provider, build and options as A |
| C: native delta, later | Same host transaction semantics as B | Explicit experimental delta consumer |

**A versus B is the first experiment.** B deliberately retains a complete export
and the backend's existing snapshot/reconciliation work. Their cost is included,
not hidden in setup. It can quantify avoided host revalidation/reinsertion even
while snapshot delivery remains O(N). Routing B back through the ordinary public
snapshot materializer is only a compatibility control, not the optimized B path;
identify any second materialization instead of crediting it as eliminated.
B versus C later isolates boundary/backend maintenance changes as far as the
measured scopes permit. Do not use a different private solver in B and attribute
the combined improvement to the host. No downstream implementation is needed
in this repository.

Start A/B with a public recording snapshot provider and a bounded program whose
complete outputs are independently known, to check canonical delivery and isolate
host transport. Then use an unchanged real ABI 5 provider on its admitted rule
subset; identify the provider and every exclusion. A recording provider is not
evidence of application solve latency. Check exact callback input count/order,
predicate names, typed values, strings and normalization in A/B, as well as
complete outputs and commit/abort receipts. Keep ordinary sessions and two-bank
windows as separately reported experiments; include bank synchronization in the
latter. Full reference evaluation and differential checks run outside counters
and timers, while both measured paths retain their production input validation.

The predeclared matrix uses live bases 8/64/256, zero/one/small-batch/full-replace
changes, insertions, removals, alternating updates and shared window suppliers.
Include sorted/reverse/duplicate/adversarial order, hash collisions, long-lived
fixed vocabulary, empty results and small/large complete IDB where supported.
Include count groups, stable aggregate subprograms and recursion with/without
orphan derivations only on providers that admit them. Test SMALL and LARGE within
their actual bounds. Admit symbolic churn only in an explicitly implemented
vocabulary mode; a fixed-vocabulary success is not churn evidence.

Pre-generate the same logical transition trace for both paths, not the measured
host's validation/index work. Measure two scopes separately: the engine operation
from submitted input to commit and result release, and the caller/window lifecycle
including production of its snapshot or delta. Report initialization, storage
planning and first transaction separately. Count failed attempts and abort work,
empty transactions, periodic cleanup and resets as well as successful updates;
do not defer maintenance past the measured sequence.

Use disjoint execution scopes for host input maintenance, vocabulary/ID mapping,
delta-to-snapshot export, window composition/catch-up, host output services,
backend algorithm, commit, abort and release/reset. Call-path attribution matters:
host emission/filter services invoked inside a backend callback are still host
work. Sum exclusive per-function counts within those scopes, reconcile them
with the complete operation and retain any unclassified residual. Do not subtract
inclusive call trees or count the four functions named by the consumer as all
host work. Record Ir/Dr/Dw for two separate repetitions, with artifacts/hashes.

For each matched successful trace and failure trace, report absolute counts and
per-attempt counts. Let `H_A` and `H_B` be those measured host totals, including
export and synchronization; host savings are `H_A - H_B` and
`(H_A - H_B) / H_A`. Report `T_A - T_B` for the complete operation separately,
alongside backend work and residuals. Positive host savings with a slower total
are not an end-to-end win. B may save nothing or cost more: that is a valid
outcome, not a reason to omit its export, rollback or small-case costs.

Also report reserved/peak/live bytes, initialization instructions, allocator
calls and bytes copied/zeroed. For timing, finish all builds first, use two A/A
pairs and counterbalanced A/B rounds on the same runner/compiler/profile, and
retain every sample. Use minima below 10 microseconds, otherwise median and p95
with their own floors. Preserve layout/address evidence and instructions by
function; equal software counts do not imply equal cycles. Do not compare times
from separate runs or infer a universal tolerance from null controls. Keep raw
evidence in artifacts, not git. An eventual Python performance claim separately
requires the complete Python lifecycle protocol.

## Decision before implementation

Agreement is requested on set/add-wins semantics and token ownership, origin
accounting and bank synchronization, the fixed-vocabulary first scope, the
persistent/scratch rollback boundary, and the A/B oracle and measurement plan.
Only then should a bounded experiment establish semantic equivalence and actual
host savings. Its result must include unsupported combinations and regressions,
with a maintainer decision on the tradeoff before public delivery.

Public signatures, negotiation/versioning, storage/diagnostic layouts and any
native delta backend follow that evidence in a separate review. This note alone
authorizes no engine implementation, ABI reservation or release.
