# Linear composition in the host delta experiment

Status: experimental follow-up to #139, based on its merge `41372dc`.
The [original protocol](host-delta-snapshot-experiment.md) and
[transaction agreement](backend-transaction-deltas.md) still bound the work.
There is no installed implementation or new public signature.

The evidence from #139 justifies examining host input maintenance. Any eventual
consumer-facing delta entry point would belong to **sessions**, with their
storage, ownership and transaction guarantees. The backend still receives an
ABI 5 snapshot and needs no migration. Host savings alone do not justify a new
backend contract. Native backend deltas remain a separate question.

## Three paths, one provider

The same diagnostic binary now accepts A (production snapshot materialization),
B (the original repeated-move delta prototype), and L (linear composition).
L is another host delta-to-snapshot path, **not** path C from the agreement,
which was reserved conceptually for a later native-delta consumer experiment.
The old B algorithm remains present; its dispatch now includes the L branch.
Do not treat its new counts as byte-identical reproduction of the old binary.
Compare all three in the new run, retaining #139's report as historical evidence.

L reuses precisely the same committed EDB, transaction scratch and two-entry
bank journal as B. It keeps validation and sorting/deduplication of the complete
raw delta, including invalid cancelling entries. For each bank catch-up step:

1. Scan sorted retained/staged facts and sorted removals with monotone cursors.
   Compact survivors once into the existing staging array.
2. Scan sorted additions against those survivors. Compact only new additions in
   the existing addition scratch. A removed-and-added fact is absent among the
   survivors, so its addition wins.
3. Check the complete expansion against the staging capacity before writing it.
   Merge the two disjoint sorted sets **backward**, into the staging array. The
   gap between the write cursor and unread survivors is the number of remaining
   additions; an unread survivor cannot be overwritten.

Each cursor moves in one direction. Composition is O(N + removals + additions)
**after** normalization, with no additional array, tombstone store or allocation.
There are still assignments/compaction writes; “no repeated moves” does not mean
“no writes.” Raw-delta sorting, complete export, emitted-result services, retained
copies and the backend are separate costs. Neither total transaction complexity
nor the application solve time is claimed to be linear.

Capacity failure can modify provisional facts/addition scratch, but never the
committed arena or published count. Raw validation precedes provisional writes;
all catch-up steps are validated before composition. Final EDB finalization still
checks per-predicate limits. Backend, work-budget or simulated late rejection
retains exactly the same committed data/generation as B. There is no threshold
that silently chooses snapshots for large deltas, and no fallback is introduced.

## Proof and measurements

The unchanged reference oracle checks complete canonical inputs, EDB, IDB,
canonical symbol IDs and receipts for A/B/L. The previous transaction conformance
checks now run against both delta implementations. An additional exhaustive
oracle covers every base/removal/addition subset of five typed native values
(two symbols, two integers, one boolean): 32³ combinations, each at roomy and
tight capacities, **65,536 cases per profile**. It checks add-wins semantics,
empty/exact/overflow capacities, unchanged published counts on rejection and
sentinels beyond the writable capacity.

The four original mutants remain. Four additional mutants must be detected:
ignored removals, treating existing additions as new, rejecting exact capacity,
and the wrong merge order. The first two also exercise equality and type
identity. This is bounded algorithm conformance, not a substitute for the full
session/window/explanation acceptance conditions in the agreement.

The matrix remains eight transactions, both profiles, all existing cases and
failure traces, engine and modeled-caller scopes. Outside the window case, one
changed fact means one removal plus one addition; a four-fact batch means four
of each, and a full replacement means N of each. The order is A1/B1/L1 followed
by L2/B2/A2. Build everything before counting. Require identical Ir/Dr/Dw for each
repeated operation and exclusive function; retain initialization separately.
Provider exclusive work must match across all three variants. Export, copies,
commit/abort, releases and bank synchronization stay inside their declared
scopes. Record both L/A and L/B, including small-delta losses if any.

Schema 2 requires `experiment.json` with the exact variants/order/trace length.
Missing L evidence cannot become a two-way success. Region and receipt identity,
duplicate records, repetitions, byte-observer receipts and complete inventories
are checked. The reporter retains all three exclusive function maps, shared
buckets and client-request residuals, with separate host and complete-operation
changes for B/A, L/A and L/B. Schema-1 reports are reproduced with their original
reporter; they are not relabeled or overwritten.

Use the existing `run_host_delta.sh` procedure. The separate primitive-byte
observer also runs L. No `memmove` request in that path would confirm elimination
of that primitive in this fixture; it does not count implicit struct assignments
or prove zero memory traffic. Retain Dr/Dw and the changed function counts.
Sanitizers, allocator traps and mutations establish correctness evidence, not
speed. The measurement is software instructions; there is no new timing campaign.

The fixed vocabulary, one-rule projection provider, modeled rather than
production windows, lack of real explanation leases, cross-incarnation token
work, storage over-reservation and excluded language features remain the limits
of #139. The next public API decision cannot skip those obligations because L
improves a benchmark. No private implementation is read or changed for this work.
