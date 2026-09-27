# Transactional window context

The 0.13.0 batch groups the unified `<maelys/datalog_window.h>` migration with
additive operations on the existing two window handles. No `program_info`,
consumer/program/backend ABI version, existing signature or public record layout
changes. The retired include path is the deliberate source migration.

## Mutable static EDB

Use `window_storage_requirements_configured` / `window_init_configured`, or
the corresponding `group_window_*` functions, to reserve a raw static-fact
capacity in `maelys_datalog_window_options_t` in addition to the existing
event/contribution capacity. Set `struct_size` to `sizeof` that record and
`flags` to zero or `MAELYS_DATALOG_WINDOW_EXPIRATION`; NULL options mean defaults. Legacy
constructors mean zero static capacity. Static input starts empty. All adapter
memory is in the queried caller arena; no operation allocates or grows storage.
The query covers both banks and checked alignment/size arithmetic, but excludes
the borrowed sessions and their result/explanation storage.

Each input bank stores a static prefix followed by ordered events/contributions,
sharing its interned text budget. The sum of reserved raw static and event slots
must fit `MAX_EDB_FACTS`, even if duplicates would make a small union. Events
retain their independent capacity and FIFO admission; adding static storage
does not silently shrink it. The group adapter's `unique_facts` limits the
**combined** union and may be up to the sum when using the new constructors.
Both sort arrays also reserve that sum. Session/program/per-predicate limits
still apply; a storage plan does not guarantee that every snapshot can solve.

`window_replace_static(window, facts, count, diagnostic)` replaces the entire
static input and immediately recomputes it with the retained events. The group
function has the same contract. NULL/0 clears it. This neither inserts an event
nor consumes an occurrence/group ID, including after that cursor is exhausted.
An authorization revocation therefore takes effect without waiting for a future
event. It can change positive, negative and aggregate conclusions.

Static facts are complete typed facts, with no implicit ID term. They use the
same domain validation, boolean normalization and string interning as normal
inputs. Raw duplicates consume static slots but have set semantics at solve.
There is no special namespace: equal static/event/policy facts are equal under
the existing runtime rules. A group-union fact survives while static input or
any retained group supplies it. Static facts do not expire on FIFO eviction.

The adapter builds the complete candidate in its other bank/session. Only after
solving succeeds and the old result lease can be released are static input,
result and views published together. All failures preserve the committed bank,
metadata and cursor, including text/capacity/aggregate/backend failures and a
prepared explanation holding the result. Release that explanation and retry.
Candidate storage may change, and external callback side effects are outside
the rollback guarantee. Backend ABI 5 sees canonical complete snapshots; commit
runs only on acceptance, and a discarded candidate is aborted through the
existing result lifecycle. No backend delta API or new capability is implied.

`*_static_facts` returns the ordered raw static slice, including duplicates.
`window_events`, group descriptors and group contributions remain event-only.
Group `facts` and `usage.unique_facts` include static input; text usage covers
both populations. All views are borrowed, read-only and invalidated on any
successful mutation/free, including a replacement with equal content. Failed
operations preserve them. Never pass a borrowed view back into a mutating
operation: input, output and diagnostic ranges must remain disjoint from the
adapter arena. Access is serialized, and reentry is rejected.

## Explicit expiration without insertion

Set `MAELYS_DATALOG_WINDOW_EXPIRATION` in the creation options to reserve
per-event/group deadline metadata in both banks. The legacy constructors and
configured zero-flag path do not reserve deadline arrays. On the checked 64-bit
layout each record is 16 bytes, so enabling expiry adds 32 bytes per event/group
slot across the two banks, plus any queried alignment padding. The opaque
adapter header also carries pointers and a watermark; always query the current
SDK's storage requirement. There is no allocation or fallback during operations.

`*_push_until(..., expires_at, ...)` adds one absolute uint64 deadline to an
otherwise normal push. Time units and the clock source belong to the caller;
the adapter reads no clock and computes no TTL. Ordinary pushes have no time
deadline and may coexist with timed pushes. Arrival order still controls FIFO
capacity. Deadlines may be out of order, and both zero and UINT64_MAX are valid.
No insertion advances the time watermark or implicitly expires other entries.

`*_expire(window, now, &expired, diagnostic)` removes **all** timed events/groups
whose deadline is **<= now**, preserving the order and IDs of survivors. The
returned count is events/groups, including empty groups, not unique facts.
Static input is preserved, and the group union retains facts supported by any
survivor or by static input. Publication recomputes the complete candidate with
the same result lease and backend commit/abort boundary as static replacement.
It consumes no ID and still works after ID exhaustion.

The watermark advances only on success. Time may remain equal, but moving
backwards is INVALID_ARGUMENT. A newly supplied deadline at or before an
already committed watermark is rejected with INVALID_ARGUMENT; no ID is
consumed. `*_expiry_watermark` returns the last committed value and a boolean
indicating whether any expire call has succeeded; before that it returns 0/false.
Time operations without the creation flag return UNSUPPORTED.

If nothing is due, expire changes only the watermark. It does not solve, invoke
a backend, replace a result or invalidate a borrowed view; a prepared explanation
can remain alive. If anything is due, publication may be blocked by that lease.
All failures preserve the committed watermark and state and leave the optional
expired-count output unchanged. In particular, removing an input can enable
facts through negation and cause an aggregate/derived-capacity failure: removal
is not inherently infallible. The caller must inspect the status before treating
the requested time as committed, release leases or adjust context, and retry.

This is an explicitly driven adapter, not a scheduler or a deadline guarantee.
No event is removed until an expire call succeeds. There are no background
threads, automatic wake-ups, late-data buffering or duration arithmetic.

## Validation and release boundary

Independent snapshot tests cover both adapters, authorization removal without
an insertion, late lease rejection with readable premises, canonical IDs,
normalization, caller text reuse, duplicate suppliers, FIFO retention, overflow,
short storage, arithmetic overflow and closed handles. The installed static and
shared SDK run the same consumer. White-box allocation guards compare complete
committed banks/metadata on rejection with engine allocators disabled. The
external recording provider checks exact canonical input and commit/abort
behavior on replacement, callback failure and clearing without an insertion.
Expiry tests add independent bounded event sequences, unordered/equal/maximal
deadlines, mixed untimed inputs, empty and multi-fact groups, survivor slice
compaction, static duplicate suppliers, no-op watermark progress, exhausted IDs,
negation-induced aggregate overflow, callback failures and byte-exact lease/
release rollback with allocators disabled. These checks make no timing or
whole-application memory claim.

Before cutting 0.13.0, build the downstream consumer against a clean installed
integrated candidate SDK and record revisions, profile, commands, outcomes and
required migrations. Include review alone is not that integration replay.
Downstream requirements arrive as written proposals; their implementation code
is not imported into this repository.
