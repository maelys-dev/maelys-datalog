/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_BACKEND_TRANSACTIONS_H
#define MAELYS_DATALOG_BACKEND_TRANSACTIONS_H
#include "datalog_resources.h"
#include "datalog_transactions.h"
#ifdef __cplusplus
extern "C" {
#endif

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
 * every subsequent base, including replacement catch-up of a window bank.
 * All packet/view pointers are callback-scoped. No reentry except SDK program,
 * input-view and output functions. No implicit snapshot fallback. */
typedef struct {
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
/* Copies identities/descriptor; code must outlive the session. NULL restores
 * ordinary reference configuration. Requires a retained-input attachment
 * before solving; ordinary session_solve is refused. Unknown version, short
 * descriptor or missing callbacks reject without modifying the configuration.
 * There is no ABI 7 extension registry or language capability bit in V1. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_backend_v7(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_v7_t *);
#ifdef __cplusplus
}
#endif
#endif
