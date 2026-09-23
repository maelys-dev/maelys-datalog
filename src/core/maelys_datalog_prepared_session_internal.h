#pragma once
#ifndef MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H
#define MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H

#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_edb_internal.h"
#include "src/core/maelys_datalog_prepared_session.h"

#define MAELYS_DATALOG_MAX_INPUT_SYMBOLS \
    (MAELYS_DATALOG_MAX_EDB_FACTS * MAELYS_DATALOG_MAX_TERMS)

struct maelys_datalog_prepared_session {
    maelys_datalog_internal_ruleset_t prepared;
    maelys_datalog_internal_ruleset_t working;
    maelys_datalog_internal_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_internal_edb_t edb;
    /* The pointer sort finishes before native facts are inserted. Reuse its
     * storage without increasing session size or changing any public layout. */
    union {
        const char *symbol_inputs[MAELYS_DATALOG_MAX_INPUT_SYMBOLS];
        maelys_datalog_edb_insert_index_t fact_index;
    };
    maelys_datalog_internal_solve_result_t *active_result;
    maelys_datalog_internal_solve_result_t *result_workspace;
};

_Static_assert(sizeof(maelys_datalog_edb_insert_index_t) <=
                   sizeof(((maelys_datalog_internal_prepared_session_t *)0)->symbol_inputs),
               "fact index must fit existing session scratch storage");

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

/* Execute the already canonicalized EDB and acquire its result lease. */
maelys_result_t maelys_datalog_prepared_session_solve_materialized_ex(
    maelys_datalog_internal_prepared_session_t *, maelys_datalog_internal_solve_result_t **,
    maelys_datalog_internal_solve_diagnostic_t *);

#endif
