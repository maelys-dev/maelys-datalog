# Last-N multi-fact event window

`<maelys/datalog_group_window.h>` adds a native C adapter to the public library.
One accepted event is a group of zero or more complete typed facts. The adapter
retains the last N groups in arrival order, builds their set union and solves the
complete snapshot. Datalog syntax is unchanged. The current contracts are
backend ABI 5 (0.11.0) and program ABI 2.
This is a reference recomputation adapter, not an incremental solver. Bindings,
new memory profiles, elastic storage and resource negotiation are separate work.

The existing `<maelys/datalog_window.h>` API remains unchanged: it still accepts
one fact per event and prepends an occurrence term. Its handles are not accepted
by this new API.

## Contributions, facts and identities

Group IDs are monotone adapter metadata in 0..INT32_MAX. They are not inserted
into predicates or terms. A group is a slice of the contributions view, identified
by its ID, offset and count. A duplicate within/across groups occupies a separate
contribution slot but appears once in the runtime union. Expiry removes a fact
only when no retained group supplies it. This tracks input contributors, not
proofs or supports of derived facts.

Equality compares predicate text, arity, value kinds and values. Booleans are
normalized by the existing input buffer: every nonzero boolean becomes true.
Integer 1, boolean true and symbol "1" remain different values. String addresses,
inactive term slots, struct padding and result-local symbol IDs do not define
equality. Facts have the same arity and meaning the application supplied.

N counts accepted groups, including empty ones. An empty group consumes an ID,
can expire the oldest group and may change the result. A rejected group advances
nothing. After accepting ID INT32_MAX, even an empty push is rejected with
PAYLOAD_TOO_LARGE; the reported cursor is INT32_MAX+1. IDs never recycle.

For N=2, A=[reading(1,5), reading(2,5)] followed by B=[reading(1,5)] gives two
groups, three contributions and two facts. The sum over reading values is 10;
the distinct-value count is 1. Empty C expires A, leaving reading(1,5) via B and
sum 5. Empty D expires B, leaving no runtime facts. Count/sum on an existing
aggregate group give zero, min/max fail; no absent grouping domain is invented.
Policy facts remain independent of window expiry.

Include an explicit occurrence field in the input facts if two otherwise equal
observations must contribute twice to sum or be distinguishable to Datalog.
Adapter group IDs are not visible to rules. Similarly, collectors must decide
how to retain dependencies older than N events; this window does not guarantee
that a retained Wasm fragment is closed over all its producers.

## Capacities and storage

Pass four immutable capacities, copied at initialization:

| Field | Admission bound |
| --- | --- |
| groups | 1..INT32_MAX+1, independent of the fact capacities |
| contributions | 1..loaded MAX_EDB_FACTS, including every duplicate |
| unique_facts | 1..contributions, for the runtime union only |
| text_bytes | Per-bank input buffer text bound, predicate/symbol bytes including NULs |

This first implementation deliberately bounds **raw retained contributions** by
the existing SDK input limit, even when their union is smaller. SMALL currently
allows 1024 such entries, LARGE 2048. Per-predicate limits, policy storage,
vocabulary, derived facts and depth impose additional restrictions at solve.
A capacity is not a promise that every policy or payload will fit.

Call `group_window_storage_requirements` before reserving the aligned caller
arena. The query checks capacities and size arithmetic. Reservation includes:

- The adapter header and two arrays of N group descriptors.
- Two existing input EDB buffers, each holding C contributions and T interned text
  bytes, including their bounded indexes and rollback scratch.
- Two C-entry public-fact arrays used for sorting and compacting the union.

Alignment padding is included. The unique-fact cap does not shrink the C-entry
sort reservation. All adapter candidate storage is reserved here; the adapter
uses constant stack, no VLA, recursion or allocator calls. The two session
reservations, results/provenance, optional explanation workspaces and engine
stack are additional. Session creation and policy compilation may allocate;
this is not a whole-lifecycle no-heap artifact.

Only the final retained suffix plus proposed group must fit in C and T. A full
window can replace its oldest group without an extra persistent group/fact slot.
Rebuilding the other bank recovers text used only by expired contributions when
the transaction commits. Rejected attempts preserve the old text and occupancy.
There is no growth, heap fallback or implicit reduction of N.

`state` reports committed groups, contributions, union size, text bytes and next
ID in O(1). `groups`, `contributions`, `facts` and `result` return borrowed views
in O(1). `facts` is sorted by predicate bytes, arity and typed values; its strings
are owned by the active input bank. These observations exclude candidate scratch
and do not claim to measure whole-engine peak memory. Storage queries describe
reservation, rather than a new per-attempt statistics API.

## Transaction and lifecycle

Create two distinct idle sessions with equal execution fingerprints, configure
any explanation storage independently, then lend their exclusive use to the
adapter. Initialization solves empty runtime input and may fail without leaving
an adapter result leased. Input/output/diagnostic ranges and session storage must
be disjoint from the exclusive, immovable caller arena. Serialize all accesses.

Push builds the retained suffix and new contributions in the candidate bank.
The existing input buffer validates every contribution's shape/text, copies
strings and normalizes booleans. An in-place heapsort followed by compaction
constructs the union in O(C log C) fact comparisons, with bounded string length.
No libc qsort is used. Domain and solve-time validation then apply to this union.

Only after candidate solving succeeds and the previous result can be released
does the adapter publish groups, contributions, union, result and cursor. No
fallible operation follows old-result release. On any failure, the entire
committed bank and persistent adapter metadata remain unchanged; candidate bank,
candidate session and attempt diagnostics may change. Application callback side
effects are not rolled back. When a failing provider supplies no diagnostic code,
the adapter supplies a neutral commit-failure diagnostic without guessing a cause.

Never free the borrowed result yourself. A prepared explanation blocks replacing
or closing its result; release it and retry. Candidate results are discarded if
publication is blocked. A successful push/free invalidates all prior borrowed
views and result pointers, while a failed push preserves them.

Close releases the result and clears references to both borrowed sessions. All
valid operations on the closed handle reject before accessing sessions, even
after their destruction, while the caller arena remains alive and intact. It may
be initialized for a new lifetime; no marker can distinguish an old handle after
that address is reused. The adapter never frees or erases the caller arena.

## Validation and integration

`test_maelys_datalog_group_window` maintains a separate FIFO with independently
owned strings. It constructs its expected set by a quadratic equality scan,
sharing neither production sorting nor adapter views, and submits that set to a
third reference session. Each successful publication compares all IDB predicates,
resolved values and canonical symbol IDs, as well as group metadata, contributions
and the runtime union. Three fixed seeds generate mixed predicate/aggregate/
negation/recursive sequences. Negative controls mutate a tuple, a group ID and an
expiration in the model and require the oracle to disagree.

Directed cases include shared facts and last-contributor removal, empty groups,
normalized booleans and typed distinctions, full replacement, each storage bound,
per-predicate saturation, renewal across 600 symbols, cursor exhaustion, rejected
aggregate overflow, prepared leases, backend faults before work and after result
emission, reentry and closed handles after sessions are destroyed.

`test_maelys_datalog_group_window_alloc` disables all instrumented engine
allocators after session creation. Init/push/read/configured and caller-owned
explanations/free run with zero allocator/free calls. Failure checks compare the
entire committed bank, including unused bytes, and adapter metadata byte-for-byte;
the candidate bank is intentionally excluded. A non-lease result-release failure
also checks cleanup and reuse. Both suites run on SMALL/LARGE and under ASan/UBSan.
The installed static and shared SDK consumers run the functional suite and the
[minimal C example](../../examples/multi_fact_window.c); C11/C++17 checks keep the
new handle opaque. These checks establish correctness/allocation, not speed.

Consumers of this additional reference must pin a published SDK carrying the
new header; a sibling checkout or copied engine implementation is not a substitute.
A future persistent backend must preserve these observable transactions with its
own commit/rollback protocol. Two alternating reference sessions do not prescribe
its architecture. ABI 5 still incurs host input materialization; whole-transaction
and derivation costs must be reported separately.

Backend ABI 5 defers acceptance through the built-in adapter's private runtime
bridge: only a published result receives `commit`. The initialization probe and
post-solve candidates rejected by a held explanation receive `destroy_result`
without commit. Capacity and text checks remain before solve. This changes no
public window signature; see the [migration addendum](../api-type-migration.md#0110--backend-abi-5).
