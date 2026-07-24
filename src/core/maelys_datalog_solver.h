#pragma once
#ifndef MAELYS_DATALOG_SOLVER_H
#define MAELYS_DATALOG_SOLVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_ruleset.h"
#include "src/core/maelys_datalog_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct maelys_datalog_solve_result maelys_datalog_solve_result_t;

typedef enum {
    MAELYS_DATALOG_SOLVE_DIAG_NONE = 0,
    MAELYS_DATALOG_SOLVE_DIAG_MAX_DEPTH,
    MAELYS_DATALOG_SOLVE_DIAG_IDB_OVERFLOW,
    MAELYS_DATALOG_SOLVE_DIAG_COMPARISON_TYPE_ERROR,
    MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_FACT,
    MAELYS_DATALOG_SOLVE_DIAG_MALFORMED_EDB,
    MAELYS_DATALOG_SOLVE_DIAG_INVALID_STATE,
    MAELYS_DATALOG_SOLVE_DIAG_INVALID_ARGUMENT,
    MAELYS_DATALOG_SOLVE_DIAG_INTERNAL_ERROR
} maelys_datalog_solve_diag_category_t;

typedef struct {
    maelys_result_t failure_error;
    maelys_datalog_deny_reason_t failure_reason;
    maelys_datalog_solve_diag_category_t category;
    uint16_t predicate_id;
    uint16_t rule_id;
    uint16_t depth;
    uint16_t depth_limit;
    uint16_t capacity;
    uint16_t count_observed;
    uint8_t lhs_kind;
    uint8_t rhs_kind;
    uint8_t comparison_op;
    uint8_t term_index;
    uint8_t arity_expected;
    uint8_t arity_observed;
    uint8_t _pad[2];
} maelys_datalog_solve_diagnostic_t;

const char *maelys_datalog_solve_diagnostic_category_name(
    maelys_datalog_solve_diag_category_t category);
maelys_result_t maelys_datalog_solve_once(const maelys_datalog_ruleset_t *ruleset,
                                          const maelys_datalog_edb_t *edb,
                                          maelys_datalog_solve_result_t **out_result);
maelys_result_t maelys_datalog_solve_once_ex(
    const maelys_datalog_ruleset_t *ruleset,
    const maelys_datalog_edb_t *edb,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag);
void maelys_datalog_solve_result_free(maelys_datalog_solve_result_t *result);
maelys_result_t maelys_datalog_validate_solved_ground_query(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    size_t arity);
maelys_result_t maelys_datalog_query_solved_ground_fact(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    const maelys_datalog_term_t *terms,
    size_t arity,
    bool *out_present);
const maelys_datalog_proof_tree_t *maelys_datalog_solve_result_proof(
    const maelys_datalog_solve_result_t *result);
maelys_result_t maelys_datalog_extract_proof_for_fact(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t *queried_fact,
    maelys_datalog_proof_tree_t *out_proof);
maelys_result_t maelys_datalog_solve_result_derived_fact_count(
    const maelys_datalog_solve_result_t *result,
    size_t *out_count);
/* Enumerates already-materialized IDB facts for a query-capable predicate.
 * This accessor never derives new facts, never resolves symbols to text, and
 * never allocates. The caller owns out_facts and chooses out_capacity.
 *
 * out_capacity == 0 is a valid count-only mode; out_facts may be NULL only in
 * that mode. On success, *out_count is the total number of matching facts found,
 * not just the number copied. Truncation is therefore observable by the caller
 * as *out_count > out_capacity. No ordering guarantee is made for copied facts. */
maelys_result_t maelys_datalog_solve_result_enumerate_predicate_facts(
    const maelys_datalog_solve_result_t *result,
    const char *predicate,
    size_t arity,
    maelys_datalog_fact_t *out_facts,
    size_t out_capacity,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif
