#pragma once
#ifndef MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H
#define MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H

#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_edb_internal.h"
#include "src/core/maelys_datalog_prepared_session.h"

#define MAELYS_DATALOG_MAX_INPUT_SYMBOLS \
    (MAELYS_DATALOG_MAX_EDB_FACTS * MAELYS_DATALOG_MAX_TERMS)

struct maelys_datalog_prepared_session {
    const maelys_datalog_internal_ruleset_t *prepared;
    /* Only the dictionary varies per transaction. The compiled snapshot stays
     * immutable, including the vocabulary exposed to extension backends. */
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_internal_fact_t *fact_pool;
    maelys_datalog_internal_edb_t edb;
    /* The pointer sort finishes before native facts are inserted. Reuse its
     * storage without increasing session size or changing any public layout. */
    const char **symbol_inputs;
    maelys_datalog_edb_insert_index_t *fact_index;
    size_t input_capacity, pool_capacity, scratch_bytes;
    size_t symbol_capacity, text_capacity;
    const char *quota_field;
    size_t quota_observed, quota_limit;
    int owns_storage;
    maelys_datalog_internal_solve_result_t *active_result;
    maelys_datalog_internal_solve_result_t *result_workspace;
};

/* The caller keeps this immutable snapshot alive until session destruction. */
maelys_result_t maelys_datalog_prepared_session_borrow(
    const maelys_datalog_internal_ruleset_t *, maelys_datalog_internal_prepared_session_t **);

/* Sized input state, initialized inside the containing session arena. The
 * dictionary and EDB pair scratch retain their build-sized storage in this
 * first delivery; effective S/T are nevertheless exact admission bounds. */
size_t maelys_datalog_prepared_session_storage_bytes(size_t input, size_t pool);
maelys_result_t maelys_datalog_prepared_session_init_sized(
    void *, const maelys_datalog_internal_ruleset_t *, size_t, size_t,
    size_t, size_t, maelys_datalog_internal_solve_result_t *,
    maelys_datalog_internal_prepared_session_t **);

void maelys_datalog_prepared_session_result_released(
    void *owner,
    maelys_datalog_internal_solve_result_t *result);

/* Shared canonical input boundary. Does not execute a solver or retain input
 * pointers. The owning public session supplies its separate result lease. */
maelys_result_t maelys_datalog_prepared_session_materialize_inputs(
    maelys_datalog_internal_prepared_session_t *, const maelys_datalog_fact_t *, size_t);

/* Optional internal explanation; public diagnostic layout remains unchanged. */
maelys_result_t maelys_datalog_prepared_session_materialize_inputs_diagnosed(
    maelys_datalog_internal_prepared_session_t *, const maelys_datalog_fact_t *, size_t,
    char *message, size_t message_capacity);

/* Validated, unique dynamic facts in a frozen private vocabulary. map has
 * MAX_SYMBOLS entries and belongs to the caller's transient scratch. */
maelys_result_t maelys_datalog_prepared_session_materialize_retained(
    maelys_datalog_internal_prepared_session_t *,
    const maelys_datalog_internal_fact_t *, size_t,
    const maelys_datalog_symbol_table_t *, maelys_datalog_symbol_id_t *map);

/* Execute the already canonicalized EDB and acquire its result lease. */
maelys_result_t maelys_datalog_prepared_session_solve_materialized_ex(
    maelys_datalog_internal_prepared_session_t *, maelys_datalog_internal_solve_result_t **,
    maelys_datalog_internal_solve_diagnostic_t *, maelys_datalog_diagnostic_t *);

#endif
