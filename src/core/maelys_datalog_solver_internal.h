#pragma once
#ifndef MAELYS_DATALOG_SOLVER_INTERNAL_H
#define MAELYS_DATALOG_SOLVER_INTERNAL_H

#include "src/core/maelys_datalog_solver.h"

typedef void (*maelys_datalog_solve_result_release_fn)(
    void *owner,
    maelys_datalog_solve_result_t *result);

void maelys_datalog_solve_result_set_release(
    maelys_datalog_solve_result_t *result,
    void *owner,
    maelys_datalog_solve_result_release_fn release);

/* Reference-backend adapter only; no query-whitelist bypass is exposed in the
 * public result API. The host reapplies query restrictions after import. */
maelys_result_t maelys_datalog_solve_result_idb_fact(
    const maelys_datalog_solve_result_t *, size_t, maelys_datalog_fact_t *);

#endif
