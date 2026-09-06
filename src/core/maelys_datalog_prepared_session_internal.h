#pragma once
#ifndef MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H
#define MAELYS_DATALOG_PREPARED_SESSION_INTERNAL_H

#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_prepared_session.h"

#define MAELYS_DATALOG_MAX_INPUT_SYMBOLS \
    (MAELYS_DATALOG_MAX_EDB_FACTS * MAELYS_DATALOG_MAX_TERMS)

struct maelys_datalog_prepared_session {
    maelys_datalog_ruleset_t prepared;
    maelys_datalog_ruleset_t working;
    maelys_datalog_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_edb_t edb;
    const char *symbol_inputs[MAELYS_DATALOG_MAX_INPUT_SYMBOLS];
    maelys_datalog_solve_result_t *active_result;
};

void maelys_datalog_prepared_session_result_released(
    void *owner,
    maelys_datalog_solve_result_t *result);

/* Shared canonical input boundary. Does not execute a solver or retain input
 * pointers. The owning public session supplies its separate result lease. */
maelys_result_t maelys_datalog_prepared_session_materialize_inputs(
    maelys_datalog_prepared_session_t *, const maelys_datalog_input_fact_t *, size_t);

#endif
