/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_BACKEND_H
#define MAELYS_DATALOG_BACKEND_H
#include "datalog_program.h"
#include "datalog_resources.h"
#include "datalog_inputs.h"
#include "datalog_extension.h"
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_BACKEND_ABI_VERSION 5u
typedef struct maelys_datalog_backend_output maelys_datalog_backend_output_t;

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
typedef struct maelys_datalog_backend_t {
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

/* Descriptors and identities are copied per session; code must outlive it.
 * Programs outlive prepared state; results keep a session lease. Callbacks are
 * trusted, deterministic, bounded, session-confined, without engine reentry
 * (except program accessors and the output functions). No implicit fallback.
 * All state returned on a failed callback is destroyed too; NULL is allowed.
 * Success means complete materialization, NOT a partial/query-local answer.
 * Capabilities are assertions tested by conformance, not correctness proofs. */

#define MAELYS_DATALOG_BACKEND_V6_ABI_VERSION 6u
/* Independent descriptor: never cast to/from maelys_datalog_backend_t (ABI 5).
 * resource_features must include SESSION_CAPACITIES; other bits are unsupported
 * in 0.14.0, including the known reserved allocator bit. The full V6 descriptor
 * is required. Compatible optional tails may follow; no callback insertion. */
typedef struct maelys_datalog_backend_v6_t {
    uint32_t abi_version;
    size_t struct_size;
    const char *name;
    const char *semantic_id;
    uint64_t capabilities;
    uint64_t resource_features;
    maelys_datalog_status_t (*storage_requirements)(
        const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *,
        size_t *out_bytes, size_t *out_alignment);
    maelys_datalog_status_t (*prepare)(
        const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *,
        const maelys_datalog_backend_storage_t *, void **out_state);
    /* Snapshot solve, complete emission, explanation and commit/abort contracts
     * retain their ABI 5 semantics and callback signatures. No delta callback. */
    maelys_datalog_status_t (*solve)(
        void *state, const maelys_datalog_fact_t *canonical_inputs,
        size_t input_count, maelys_datalog_backend_output_t *,
        void **out_result_state, maelys_datalog_diagnostic_t *);
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
    void (*commit)(void *state, void *result_state);
    void (*destroy_result)(void *state, void *result_state);
    void (*destroy)(void *state);
} maelys_datalog_backend_v6_t;
#define MAELYS_DATALOG_BACKEND_V6_SIZE sizeof(maelys_datalog_backend_v6_t)


#define MAELYS_DATALOG_BACKEND_V7_ABI_VERSION 7u
#define MAELYS_DATALOG_BACKEND_INPUT_VERSION 1u
#define MAELYS_DATALOG_BACKEND_INPUT_REPLACE 1u
#define MAELYS_DATALOG_BACKEND_INPUT_DELTA 2u
typedef struct maelys_datalog_backend_input_view maelys_datalog_backend_input_view_t;
typedef struct {
    size_t struct_size;
    uint32_t contract_version, kind;
    maelys_datalog_input_base_t base, next;
    const maelys_datalog_backend_input_view_t *additions, *removals;
    size_t addition_count, removal_count;
} maelys_datalog_backend_input_t;

/* Export one normalized typed fact. View and returned strings are borrowed
 * ONLY during solve; copy retained values/text into provider-owned storage.
 * NOT_FOUND for an out-of-range index, output unchanged on failure. No public
 * symbol IDs, allocation or promised enumeration order. NULL/empty view is
 * valid only when its packet count is zero. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_backend_input_at(
    const maelys_datalog_backend_input_view_t *, size_t, maelys_datalog_fact_t *out);
typedef maelys_datalog_status_t (*maelys_datalog_backend_transaction_solve_t)(
    void *, const maelys_datalog_backend_input_t *, maelys_datalog_backend_output_t *,
    void **out_result_state, maelys_datalog_diagnostic_t *);

/* Independent type, NEVER cast as ABI 5/6. Fixed normalized resources and
 * storage/prepare retain ABI 6 semantics; output, explanations, commit/abort
 * and destruction retain ABI 5 semantics. Complete IDB emission is required.
 * REPLACE supplies the complete dynamic EDB in additions; DELTA applies
 * removals then additions (additions win, absent removals are no-ops). The host
 * may suppress redundant additions. Compiled facts remain in the program.
 * First base is empty at generation zero; adopt next only in commit. Check
 * every subsequent base, including delta catch-up of a window bank.
 * All packet/view pointers are callback-scoped. No reentry except SDK program,
 * input-view and output functions. No implicit snapshot fallback. */
typedef struct maelys_datalog_backend_v7_t {
    uint32_t abi_version;
    size_t struct_size;
    const char *name, *semantic_id;
    uint64_t capabilities, resource_features;
    maelys_datalog_status_t (*storage_requirements)(const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *, size_t *, size_t *);
    maelys_datalog_status_t (*prepare)(const maelys_datalog_program_t *,
        const maelys_datalog_session_resources_t *, const maelys_datalog_backend_storage_t *, void **);
    maelys_datalog_backend_transaction_solve_t solve;
    maelys_datalog_status_t (*explanation_storage_requirements)(void *, void *,
        maelys_datalog_explanation_kind_t, size_t *, size_t *);
    maelys_datalog_status_t (*explanation_prepare)(void *, void *, maelys_datalog_explanation_kind_t,
        const char *, const maelys_datalog_value_t *, size_t, void *, size_t, size_t *);
    maelys_datalog_status_t (*explanation_write_text)(void *, void *, maelys_datalog_explanation_kind_t,
        const void *, char *, size_t);
    void (*commit)(void *, void *);
    void (*destroy_result)(void *, void *);
    void (*destroy)(void *);
} maelys_datalog_backend_v7_t;
#define MAELYS_DATALOG_BACKEND_V7_SIZE sizeof(maelys_datalog_backend_v7_t)
#ifdef __cplusplus
}
#endif
#endif
