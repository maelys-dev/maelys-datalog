/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_BACKEND_H
#define MAELYS_DATALOG_BACKEND_H
#include "datalog_program.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_BACKEND_ABI_VERSION 2u
typedef struct maelys_datalog_backend_output maelys_datalog_backend_output_t;

/* Emit the complete derived IDB, including non-query helpers. The core copies,
 * validates and deduplicates facts. Derived symbols must already belong to the
 * input/program vocabulary. A failed solve exposes no partial result. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_emit(
    maelys_datalog_backend_output_t *, const maelys_datalog_public_fact_t *);
/* Cooperative per-backend work units, not comparable across algorithms and
 * not a sandbox or a wall-clock deadline. Charge before bounded units of work.
 * A charge/emit/filter error remains fatal even if the backend ignores it. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_backend_charge(maelys_datalog_backend_output_t *, uint64_t units);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_filter(
    maelys_datalog_backend_output_t *, const char *name, const char *semantic_id,
    const unsigned char *value, size_t value_length, const unsigned char *pattern,
    size_t pattern_length, int *out_matched);

typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    uint64_t capabilities;
    maelys_datalog_status_t (*prepare)(const maelys_datalog_program_t *, void **out_state);
    maelys_datalog_status_t (*solve)(void *state,
                                     const maelys_datalog_public_fact_t *canonical_inputs,
                                     size_t input_count, maelys_datalog_backend_output_t *,
                                     void **out_result_state, maelys_datalog_public_diagnostic_t *);
    /* Required when EXPLAIN_TRUE is advertised. Read-only: never re-solve. */
    maelys_datalog_status_t (*explain_true)(void *state, void *result_state, const char *,
                                            const maelys_datalog_public_value_t *, size_t, char *,
                                            size_t, size_t *);
    /* Required when EXPLAIN_FALSE is advertised. Same read-only buffer contract. */
    maelys_datalog_status_t (*explain_false)(void *state, void *result_state, const char *,
                                             const maelys_datalog_public_value_t *, size_t, char *,
                                             size_t, size_t *);
    void (*destroy_result)(void *state, void *result_state);
    void (*destroy)(void *state);
} maelys_datalog_backend_t;

typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const maelys_datalog_backend_t *backend; /* NULL selects the reference. */
    uint64_t required_capabilities;
    /* Zero selects 1,048,576 host work units. Nonzero requires WORK_LIMIT. */
    uint64_t work_limit;
} maelys_datalog_session_options_t;

/* Descriptors and identities are copied per session; code must outlive it.
 * Programs outlive prepared state; results keep a session lease. Callbacks are
 * trusted, deterministic, bounded, session-confined, without engine reentry
 * (except program accessors and the output functions). No implicit fallback.
 * All state returned on a failed callback is destroyed too; NULL is allowed.
 * Success means complete materialization, NOT a partial/query-local answer.
 * Capabilities are assertions tested by conformance, not correctness proofs. */
MAELYS_DATALOG_API const maelys_datalog_backend_t *maelys_datalog_backend_reference(void);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_create_ex(
    const maelys_datalog_policy_t *, size_t policy_index, const maelys_datalog_session_options_t *,
    maelys_datalog_session_t **);
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_session_program(const maelys_datalog_session_t *, const maelys_datalog_program_t **);
/* Existing session_fingerprint remains the policy authority identity. This
 * separate execution identity also binds backend/version/options. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_execution_fingerprint(
    const maelys_datalog_session_t *, char out[MAELYS_DATALOG_PUBLIC_FINGERPRINT_BYTES]);

#ifdef __cplusplus
}
#endif
#endif
