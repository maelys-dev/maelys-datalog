/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_GROUP_WINDOW_H
#define MAELYS_DATALOG_GROUP_WINDOW_H
#include <maelys/datalog.h>
#ifdef __cplusplus
extern "C" {
#endif

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
 * This API is separate from datalog_window.h, whose semantics are unchanged.
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
