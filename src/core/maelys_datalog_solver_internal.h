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

#endif
