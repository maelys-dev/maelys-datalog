/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_INPUTS_H
#define MAELYS_DATALOG_INPUTS_H
#include <maelys/datalog.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MAELYS_DATALOG_INPUT_CONTRACT_VERSION 1u
typedef struct maelys_datalog_session_inputs maelys_datalog_session_inputs_t;
typedef struct {
    size_t struct_size;
    uint32_t contract_version, reserved;
    size_t fact_capacity, addition_capacity, removal_capacity;
    const char *const *symbols;
    size_t symbol_count;
} maelys_datalog_input_options_t;
#define MAELYS_DATALOG_INPUT_OPTIONS_INIT \
    {sizeof(maelys_datalog_input_options_t), MAELYS_DATALOG_INPUT_CONTRACT_VERSION, 0, 0, 0, 0, NULL, 0}
typedef struct { uint64_t incarnation, generation; } maelys_datalog_input_base_t;

/* Opt-in, fixed-vocabulary retained dynamic EDB. Compiled facts are separate.
 * Initialize before the session's first solve. Copies options and vocabulary;
 * supplied input strings are borrowed only for each call. No engine allocation
 * in sizing/init/replace/apply/release. Caller storage must be aligned and must
 * not overlap any live session, provider, explanation or attachment storage.
 * An unknown added/replacement symbol is INVALID_FIELD. An unknown removed
 * symbol denotes an absent fact, after all raw record validation. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_storage_requirements(
    const maelys_datalog_session_t *session, const maelys_datalog_input_options_t *options,
    size_t *out_bytes, size_t *out_alignment);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_init(
    maelys_datalog_session_t *session, const maelys_datalog_input_options_t *options,
    void *storage, size_t storage_bytes, maelys_datalog_session_inputs_t **out_inputs);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_base(
    const maelys_datalog_session_inputs_t *inputs, maelys_datalog_input_base_t *out_base);
/* Both entries require the current base. Raw limits apply before deduplication;
 * replace uses fact_capacity, apply uses the two independently declared batch
 * capacities. Additions win over removals; absent removals are no-ops.
 * Success (even a set no-op) publishes a new base and an ordinary leased result.
 * Failure preserves committed facts/base and sets *out_result to NULL; scratch may
 * change. Result/explanation leases forbid the next operation. Ordinary public
 * session_solve is unavailable while attached; windows use replace internally. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_replace(
    maelys_datalog_session_inputs_t *inputs, maelys_datalog_input_base_t base,
    const maelys_datalog_fact_t *facts, size_t fact_count,
    maelys_datalog_result_t **out_result, maelys_datalog_diagnostic_t *out_diagnostic);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_apply(
    maelys_datalog_session_inputs_t *inputs, maelys_datalog_input_base_t base,
    const maelys_datalog_fact_t *added, size_t added_count,
    const maelys_datalog_fact_t *removed, size_t removed_count,
    maelys_datalog_result_t **out_result, maelys_datalog_diagnostic_t *out_diagnostic);
/* Release only with no live result. Session destruction is refused while an
 * attachment is live. Released storage may be reused with a fresh session;
 * its old base never becomes valid for another attachment. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_inputs_free(
    maelys_datalog_session_inputs_t *inputs);
#ifdef __cplusplus
}
#endif
#endif
