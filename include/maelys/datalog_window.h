/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_WINDOW_H
#define MAELYS_DATALOG_WINDOW_H
#include <maelys/datalog.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Reserve per-event/group deadline metadata in both caller-owned banks. */
#define MAELYS_DATALOG_WINDOW_EXPIRATION 1u

/* Extension options shared by both adapters. Initialize every member.
 * struct_size must equal sizeof(maelys_datalog_window_options_t). flags accepts
 * only MAELYS_DATALOG_WINDOW_EXPIRATION. NULL options select legacy defaults. */
typedef struct {
    size_t struct_size;
    size_t static_fact_capacity; /* Raw static slots in addition to event slots. */
    uint32_t flags;
} maelys_datalog_window_options_t;

/* Single-fact events with generated occurrence IDs. */
typedef struct maelys_datalog_window maelys_datalog_window_t;

/* Last N accepted events, in arrival order, recomputed as a complete snapshot.
 * One push is one transaction and one EDB fact. Its first term is a generated
 * integer occurrence ID; the caller supplies the predicate and remaining 0..3
 * terms. Thus identical payloads remain distinct source facts for sum, whereas
 * count of a payload projection still counts distinct typed values.
 * There is no clock, implicit grouping, batching or Datalog syntax extension.
 *
 * The adapter borrows TWO distinct, idle sessions with equal execution
 * fingerprints, and owns their use until window_free succeeds. Do not solve,
 * free or share either session during that interval. Configure each separately
 * (including separate explanation storage) before initialization. The adapter
 * never creates or destroys sessions and never allocates or frees memory.
 * Reference sessions still allocate at creation; their usual runtime allocation
 * guarantee and explanation exceptions apply. Custom backends/filters retain
 * their own contract. This is not a whole-engine zero-heap claim.
 *
 * Storage contains two N-entry input buffers and two text arenas. text_capacity
 * is PER arena, using input_edb's interning and capacity contract. Sessions,
 * results/provenance, explanation workspaces and stack are additional memory.
 * No automatic capacity growth, heap fallback or window shrink occurs.
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_storage_requirements(
    size_t event_capacity, size_t text_capacity, size_t *out_bytes, size_t *out_alignment);

/* first_occurrence is in [0, INT32_MAX]. Initialization solves the empty EDB
 * and can therefore fail (for example a policy aggregate overflow). On error
 * *out_window is NULL; storage/scratch may change, but no window is created and
 * no adapter result is left leased. Sessions must already be idle.
 *
 * Storage must have the queried alignment and size and remain exclusive and
 * immovable until free. It must not overlap sessions, inputs or outputs.
 * Insufficient storage -> STORAGE_TOO_SMALL; malformed arguments/capacities,
 * identical sessions or different execution fingerprints -> INVALID_ARGUMENT.
 * Serialize ALL access to the window, its results and borrowed sessions.
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_init(
    void *storage, size_t storage_bytes, size_t event_capacity, size_t text_capacity,
    uint32_t first_occurrence, maelys_datalog_session_t *session_a,
    maelys_datalog_session_t *session_b, maelys_datalog_window_t **out_window,
    maelys_datalog_diagnostic_t *out_diagnostic);

/* Expire the oldest event if full, append, then solve on the other session.
 * Publish the new window/result/next ID together, only after success. On ANY
 * failure all previously committed entries/text, result and next ID remain
 * valid; candidate scratch may change. External callback side effects cannot
 * be rolled back. Inputs are copied before returning and must not overlap the
 * window's storage. out_occurrence is optional and unchanged on failure.
 *
 * IDs never wrap or recycle. After INT32_MAX is committed, further pushes fail
 * with PAYLOAD_TOO_LARGE; state reports INT32_MAX+1 as the exhausted cursor.
 * Prepared explanations on the old result block publication with INVALID_STATE
 * (possibly after candidate work). Release them and retry the same event.
 */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_push(
    maelys_datalog_window_t *window, const char *predicate,
    const maelys_datalog_value_t *values, size_t value_count,
    uint32_t *out_occurrence, maelys_datalog_diagnostic_t *out_diagnostic);

/* Borrow the latest result, including the initial empty snapshot. Do NOT call
 * result_free: the window owns this lease. Query/enumerate/explanation APIs
 * apply normally. A successful push or window_free invalidates this pointer
 * and all its views. A failed push preserves them. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_result(
    const maelys_datalog_window_t *window, maelys_datalog_result_t **out_result);
/* Ordered committed input, with generated IDs; same borrowed-view rules as
 * input_edb_view. Invalidated on successful push/free, preserved on failure. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_events(
    const maelys_datalog_window_t *window,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_state(
    const maelys_datalog_window_t *window, size_t *out_count, uint64_t *out_next_occurrence);
/* Interned predicate/symbol bytes (including NULs) used by the COMMITTED input
 * bank, and its text capacity. Excludes candidate scratch, indexes, session and
 * policy storage. O(1), no allocation; rejection preserves the reported usage.
 * Expiry can recover bytes on commit. Free text does not guarantee that a future
 * push meets the engine's other bounds. Both outputs are required and unchanged
 * on error. This reports occupancy, not a peak or a reservation for a next push. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_text_usage(
    const maelys_datalog_window_t *window, size_t *out_used, size_t *out_capacity);
/* Release the current result and return both borrowed sessions to the caller.
 * Does not free/erase caller storage. Live prepared explanations -> INVALID_STATE,
 * leaving the window usable. Success marks the handle closed and clears borrowed
 * references. While the caller arena remains alive and unmodified, subsequent
 * operations with valid arguments (including a second free) return INVALID_STATE
 * without accessing sessions, even after those sessions have been destroyed.
 * window_init may reuse the arena for a NEW lifetime. Once the arena is released,
 * repurposed or reinitialized, old handles/views must not be used; the marker
 * cannot protect a dangling arena pointer or distinguish an old pointer after
 * address reuse. Closed-handle rejection leaves accessor and occurrence outputs
 * unchanged; push still reports its error through the optional diagnostic. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_free(
    maelys_datalog_window_t *window);

/* Optional mutable static EDB (additive API). The legacy constructors above
 * are equivalent to static_fact_capacity == 0. Both banks reserve
 * event_capacity + static_fact_capacity raw slots (sum <= MAX_EDB_FACTS),
 * sharing text_capacity per bank; events retain their independent N bound.
 * options.static_fact_capacity selects the static reservation.
 * Static facts are complete facts: no generated ID is prepended. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_storage_requirements_configured(
    size_t event_capacity, size_t text_capacity, const maelys_datalog_window_options_t *options,
    size_t *out_bytes, size_t *out_alignment);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_init_configured(
    void *storage, size_t storage_bytes, size_t event_capacity, size_t text_capacity,
    const maelys_datalog_window_options_t *options, uint32_t first_occurrence,
    maelys_datalog_session_t *session_a, maelys_datalog_session_t *session_b,
    maelys_datalog_window_t **out_window, maelys_datalog_diagnostic_t *out_diagnostic);

/* Replace the ENTIRE static EDB and solve it together with retained events,
 * immediately, without an insertion or ID consumption. NULL/0 clears it.
 * Static capacity counts raw facts, including duplicates. Shape/text validation,
 * boolean normalization and runtime set semantics match session inputs. Static
 * facts never expire with the FIFO. This operation does not modify policy facts.
 * EVERY failure (including a live explanation lease) preserves committed static
 * facts, events, text, result and cursor. Candidate work can run before rejection.
 * On success all borrowed views/results are invalidated, even for equal input.
 * Inputs must not overlap the adapter arena; they are copied before returning.
 * No allocation/free, growth or fallback. Callback side effects are not rolled
 * back. text_usage includes BOTH static and event text; events/state count ONLY
 * events. All original lifetime, serialization and session contracts apply. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_replace_static(
    maelys_datalog_window_t *window, const maelys_datalog_fact_t *facts, size_t fact_count,
    maelys_datalog_diagnostic_t *out_diagnostic);
/* Ordered raw static facts, including duplicates; O(1), borrowed like events. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_static_facts(
    const maelys_datalog_window_t *window,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);

/* Explicit time expiry, enabled at creation with WINDOW_EXPIRATION. Time is
 * caller-defined uint64 units, without reading a clock or computing a duration.
 * push_until is push with an absolute deadline; ordinary push has no deadline.
 * No insertion implicitly advances time or expires other events. FIFO capacity
 * still applies. Deadlines may arrive out of order and UINT64_MAX is valid.
 * Once expire has committed a watermark, a deadline <= it is INVALID_ARGUMENT.
 *
 * expire removes ALL timed events with deadline <= now, retaining order and
 * static input, then recomputes/publishes without consuming an ID. now may equal
 * the last committed watermark but cannot decrease. Failure preserves the
 * watermark, facts, cursor, views and result, including on late lease rejection.
 * out_expired is optional, counts events and remains unchanged on failure.
 *
 * If no event is due, only the watermark advances: no solve/callback/result
 * replacement, borrowed views remain valid and live explanations do not block.
 * Otherwise successful expiry invalidates views just like a push/replacement.
 * Recompute may fail even on removal (e.g. aggregate overflow or derivation
 * growth after negation). Retry or change context; no event is silently removed.
 * Unsupported when deadline storage was not enabled. No allocation or fallback.
 * After ID exhaustion, expire and replace_static remain usable. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_push_until(
    maelys_datalog_window_t *window, const char *predicate,
    const maelys_datalog_value_t *values, size_t value_count, uint64_t expires_at,
    uint32_t *out_occurrence, maelys_datalog_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_expire(
    maelys_datalog_window_t *window, uint64_t now, size_t *out_expired,
    maelys_datalog_diagnostic_t *out_diagnostic);
/* O(1); returns (0,false) before the first successful expire; outputs required
 * and unchanged on failure. Only available with expiration enabled. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_expiry_watermark(
    const maelys_datalog_window_t *window, uint64_t *out_now, int *out_has_now);

/* Groups of complete facts, with IDs kept as adapter metadata. */
typedef struct maelys_datalog_group_window maelys_datalog_group_window_t;

typedef struct {
    size_t groups;        /* 1..INT32_MAX+1; independent of fact capacities. */
    size_t contributions; /* 1..loaded MAX_EDB_FACTS, BEFORE deduplication. */
    size_t unique_facts;  /* 1..contributions; runtime union, excluding policy/IDB. */
    size_t text_bytes;    /* Per bank, input_edb's interned text capacity. */
} maelys_datalog_group_window_capacities_t;

typedef struct {
    uint32_t id;
    size_t fact_offset;   /* Slice of the committed contributions view. */
    size_t fact_count;    /* Includes duplicates; zero is a valid group. */
} maelys_datalog_event_group_t;

typedef struct {
    size_t groups;
    size_t contributions;
    size_t unique_facts;
    size_t text_bytes;    /* Interned predicate/symbol bytes, including NULs. */
    uint64_t next_group;  /* INT32_MAX+1 denotes exhaustion. */
} maelys_datalog_group_window_usage_t;

/* Last N ACCEPTED groups in arrival order, with complete snapshot recomputation.
 * Each group contributes zero or more complete typed facts. Group IDs are adapter
 * metadata, never injected into facts. Equal facts within/across groups occur
 * once in the runtime union; expiry removes a fact only with its last supplying
 * group. Include an occurrence term explicitly if observations must be distinct.
 * Boolean nonzero values normalize to true, as in input_edb; integers, booleans
 * and symbols remain distinct. Strings compare by content, never by pointer.
 * Empty groups consume a slot and an ID and can expire the oldest group.
 * Group and single-fact window handles are distinct; the single-fact functions
 * above retain their occurrence-ID semantics.
 *
 * Fixed storage: TWO raw contribution buffers with shared text, TWO group arrays
 * and TWO contribution-sized fact arrays for sorting/compaction. unique_facts
 * is an admission limit, not a reduction of that sort reservation. The returned
 * size includes both banks and metadata, covering all adapter attempt scratch;
 * stack use is constant (no VLA/recursion). Session/result/provenance/explanation
 * reservations and their stack are additional. No adapter allocation/free at
 * init, push, read or close, no growth/fallback/shrink. This does not certify a
 * whole-engine zero-heap lifecycle. Runtime allocation guarantees are those of
 * the selected backend/callbacks. Both borrowed sessions must be distinct, idle,
 * and have equal execution fingerprints; their exclusive use lasts until close.
 *
 * This first version bounds retained raw contributions by MAX_EDB_FACTS even
 * when many duplicates would produce a small union. Engine policy, per-predicate,
 * vocabulary, depth and derived-fact limits still apply when solving that union.
 * All size calculations are checked. Outputs are unchanged on query failure. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_storage_requirements(
    const maelys_datalog_group_window_capacities_t *capacities,
    size_t *out_bytes, size_t *out_alignment);

/* Caller arena must be aligned, exclusive, immovable and disjoint from inputs,
 * outputs, diagnostics, capacities and sessions. Serialize all access, including
 * borrowed results. Configure sessions and separate explanation arenas first;
 * do not solve/free/share sessions until window_free succeeds. Capacities are
 * copied. first_group is 0..INT32_MAX. Init solves empty runtime input and may
 * fail; *out_window is NULL on failure and no adapter result is left leased.
 * Scratch may change on failed init. Misalignment/bad capacities or mismatched
 * sessions -> INVALID_ARGUMENT; insufficient arena -> STORAGE_TOO_SMALL. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_init(
    void *storage, size_t storage_bytes,
    const maelys_datalog_group_window_capacities_t *capacities, uint32_t first_group,
    maelys_datalog_session_t *session_a, maelys_datalog_session_t *session_b,
    maelys_datalog_group_window_t **out_window,
    maelys_datalog_diagnostic_t *out_diagnostic);

/* A whole group is one transaction. NULL facts is valid only at count zero.
 * Retain the suffix (expire first if full), copy/validate contributions, build
 * the typed union, solve on the other session, then publish everything together.
 * Raw contribution/text capacity applies to the FINAL retained suffix + group,
 * with no extra persistent slot required for the expiring group. Capacity
 * exhaustion -> PAYLOAD_TOO_LARGE; engine validation/statuses pass through.
 * Inputs are copied before success and must not overlap the window's arena.
 * EVERY failure preserves committed groups/contributions/text/union, the result
 * and cursor, byte-for-byte. Candidate scratch/session and diagnostics may change.
 * External callback side effects cannot be rolled back. Reentry -> INVALID_STATE.
 * A prepared explanation may block publication after candidate work; release it
 * and retry. No rejected group consumes an ID. IDs never wrap/recycle; exhaustion
 * -> PAYLOAD_TOO_LARGE. Optional out_group_id is unchanged on failure. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_push(
    maelys_datalog_group_window_t *window,
    const maelys_datalog_fact_t *facts, size_t fact_count,
    uint32_t *out_group_id, maelys_datalog_diagnostic_t *out_diagnostic);

/* All views/results are borrowed and valid until successful push/free. Failure
 * preserves them. Do NOT result_free the borrowed result or modify these views.
 * Read calls are O(1), allocate nothing and leave outputs unchanged on failure.
 * State describes COMMITTED occupancy, not per-attempt or whole-engine peak use.
 * Group slices address ordered contributions; facts exposes the sorted unique
 * union (predicate bytes, arity, then typed values), not policy facts or IDB. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_state(
    const maelys_datalog_group_window_t *window, maelys_datalog_group_window_usage_t *out_usage);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_groups(
    const maelys_datalog_group_window_t *window,
    const maelys_datalog_event_group_t **out_groups, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_contributions(
    const maelys_datalog_group_window_t *window,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_facts(
    const maelys_datalog_group_window_t *window,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_result(
    const maelys_datalog_group_window_t *window, maelys_datalog_result_t **out_result);

/* Static EDB variant: contributions remains the raw EVENT contribution bound;
 * static_fact_capacity is a separate raw bound. Their sum must fit MAX_EDB_FACTS.
 * The two input/sort banks reserve that sum; text_bytes is shared per bank.
 * unique_facts bounds the COMBINED union and may be up to that sum with these
 * constructors. Legacy constructors are equivalent to static_fact_capacity=0.
 * Capacities, existing struct layouts and backend/program ABIs are unchanged. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_storage_requirements_configured(
    const maelys_datalog_group_window_capacities_t *capacities,
    const maelys_datalog_window_options_t *options,
    size_t *out_bytes, size_t *out_alignment);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_init_configured(
    void *storage, size_t storage_bytes,
    const maelys_datalog_group_window_capacities_t *capacities,
    const maelys_datalog_window_options_t *options,
    uint32_t first_group, maelys_datalog_session_t *session_a, maelys_datalog_session_t *session_b,
    maelys_datalog_group_window_t **out_window, maelys_datalog_diagnostic_t *out_diagnostic);
/* Same replacement/rollback/lease contract as window_replace_static. No group or
 * ID is consumed. Groups/contributions remain event-only views; facts and
 * usage.unique_facts expose the combined union, usage.text_bytes shared text.
 * A fact survives expiry while supplied by any retained group OR static input. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_replace_static(
    maelys_datalog_group_window_t *window, const maelys_datalog_fact_t *facts, size_t fact_count,
    maelys_datalog_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_static_facts(
    const maelys_datalog_group_window_t *window,
    const maelys_datalog_fact_t **out_facts, size_t *out_count);

/* Same explicit-time contract as the single-fact adapter. One deadline belongs
 * to one group, including an empty group; out_expired counts groups, not facts.
 * Expiry preserves arrival order, IDs and event-relative contribution slices.
 * Duplicate facts survive while a retained group or static EDB supplies them. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_push_until(
    maelys_datalog_group_window_t *window, const maelys_datalog_fact_t *facts,
    size_t fact_count, uint64_t expires_at, uint32_t *out_group_id,
    maelys_datalog_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_expire(
    maelys_datalog_group_window_t *window, uint64_t now, size_t *out_expired,
    maelys_datalog_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_expiry_watermark(
    const maelys_datalog_group_window_t *window, uint64_t *out_now, int *out_has_now);

/* Close releases the result and returns both sessions; it never frees/erases
 * the caller arena. A result-release failure leaves the window usable. Success
 * marks it closed: valid subsequent calls return INVALID_STATE before touching
 * sessions, even if sessions have been destroyed. This guarantee requires the
 * arena to stay alive and intact. Reinitialization is allowed but starts a NEW
 * lifetime; old handles/views must not be used after storage reuse. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_group_window_free(
    maelys_datalog_group_window_t *window);
#ifdef __cplusplus
}
#endif
#endif
