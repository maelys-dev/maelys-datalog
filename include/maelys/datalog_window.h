/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_WINDOW_H
#define MAELYS_DATALOG_WINDOW_H
#include <maelys/datalog.h>
#ifdef __cplusplus
extern "C" {
#endif

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
    maelys_datalog_public_diagnostic_t *out_diagnostic);

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
    const maelys_datalog_public_value_t *values, size_t value_count,
    uint32_t *out_occurrence, maelys_datalog_public_diagnostic_t *out_diagnostic);

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
    const maelys_datalog_public_fact_t **out_facts, size_t *out_count);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_state(
    const maelys_datalog_window_t *window, size_t *out_count, uint64_t *out_next_occurrence);
/* Release the current result and return both borrowed sessions to the caller.
 * Does not free/erase caller storage. Live prepared explanations -> INVALID_STATE,
 * leaving the window usable. The handle is invalid after a successful free. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_window_free(
    maelys_datalog_window_t *window);

#ifdef __cplusplus
}
#endif
#endif
