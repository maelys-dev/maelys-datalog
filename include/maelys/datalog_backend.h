/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_BACKEND_H
#define MAELYS_DATALOG_BACKEND_H
#include "datalog_program.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_BACKEND_ABI_VERSION 5u
typedef struct maelys_datalog_backend_output maelys_datalog_backend_output_t;

/* Caller-owned, aligned, immovable storage, disjoint from other live storage,
 * inputs and outputs. The buffer lives through session destruction. The host
 * copies this descriptor, never the buffer, and never accesses or frees its
 * contents. NULL bytes is allowed only with size == 0 and zero requirements.
 * alignment is a nonzero power of two <= alignof(max_align_t). */
typedef struct {
    size_t struct_size;
    void *bytes;
    size_t size;
    size_t alignment;
} maelys_datalog_backend_storage_t;

/* Emit the complete derived IDB, including non-query helpers. The core copies,
 * validates and deduplicates facts. Derived symbols must already belong to the
 * input/program vocabulary. A failed solve exposes no partial result. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_emit(
    maelys_datalog_backend_output_t *, const maelys_datalog_fact_t *);
/* Cooperative per-backend work units, not comparable across algorithms and
 * not a sandbox or a wall-clock deadline. Charge before bounded units of work.
 * A charge/emit/filter error remains fatal even if the backend ignores it. */
MAELYS_DATALOG_API maelys_datalog_status_t
maelys_datalog_backend_charge(maelys_datalog_backend_output_t *, uint64_t units);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_filter(
    maelys_datalog_backend_output_t *, const char *name, const char *semantic_id,
    const unsigned char *value, size_t value_length, const unsigned char *pattern,
    size_t pattern_length, int *out_matched);

/* Extension-author descriptor. Ordinary consumers need only datalog.h and
 * its opaque session configuration, not this callback contract. */
typedef struct {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    uint64_t capabilities;
    /* Read-only, deterministic for the program, without allocation or reentry.
     * Called once per session creation before prepare (explicit public queries
     * are separate calls). Zero bytes is valid; alignment must be a nonzero
     * power of two <= alignof(max_align_t), even for zero bytes. */
    maelys_datalog_status_t (*storage_requirements)(const maelys_datalog_program_t *,
                                                   size_t *out_bytes, size_t *out_alignment);
    /* storage is non-NULL and valid for this call only; storage->bytes is the
     * caller's exact pointer. Retain the buffer, not the descriptor pointer.
     * Absent storage is represented by {sizeof(...), NULL, 0, 1}. */
    maelys_datalog_status_t (*prepare)(const maelys_datalog_program_t *,
                                      const maelys_datalog_backend_storage_t *, void **out_state);
    maelys_datalog_status_t (*solve)(void *state,
                                     const maelys_datalog_fact_t *canonical_inputs,
                                     size_t input_count, maelys_datalog_backend_output_t *,
                                     void **out_result_state, maelys_datalog_diagnostic_t *);
    /* Caller-owned explanation storage introduced in ABI 3 is retained in ABI 5.
     * The versioned common diagnostic protocol introduced in ABI 4 also remains.
     * All three are required if either EXPLAIN capability is advertised; the
     * host calls only supported kinds. No allocator calls, acquired resources,
     * engine reentry or retained query-string pointers. Result state stays alive
     * until all explanations are released. Scratch belongs in the same storage;
     * bounded automatic locals are allowed. No destroy hook is needed: this
     * storage contains only values and references borrowed from the live result.
     *
     * storage_requirements is read-only and deterministic for (result, kind).
     * Alignment must be a nonzero power of two <= alignof(max_align_t), size > 0.
     * prepare extracts once and returns the EXACT text length excluding NUL;
     * SIZE_MAX is invalid. On failure storage may change, retained state may not.
     * write_text only formats the prepared storage; never extracts/searches again.
     * The host checks capacity first. Successful output is NUL-terminated and
     * exactly the prepared length. All callbacks are trusted and bounded. */
    maelys_datalog_status_t (*explanation_storage_requirements)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        size_t *out_bytes, size_t *out_alignment);
    maelys_datalog_status_t (*explanation_prepare)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        const char *, const maelys_datalog_value_t *, size_t,
        void *storage, size_t storage_bytes, size_t *out_text_size);
    maelys_datalog_status_t (*explanation_write_text)(
        void *state, void *result_state, maelys_datalog_explanation_kind_t,
        const void *storage, char *text, size_t capacity);
    /* Exactly once per accepted result, after host validation/copy/deduplication
     * and installation; for windows, only after publication is irrevocable.
     * Never called for an abandoned candidate (including window init probes).
     * Infallible, no allocation or reentry; precedes every explanation callback.
     * solve must keep previously committed backend state intact until commit.
     * destroy_result without commit means abort; after commit it means release.
     * A NULL result_state is valid and does not suppress either callback. */
    void (*commit)(void *state, void *result_state);
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
/* Consumer fingerprint accessors are declared in datalog.h, included above. */

#ifdef __cplusplus
}
#endif
#endif
