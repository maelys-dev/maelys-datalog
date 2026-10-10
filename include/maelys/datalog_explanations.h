/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_EXPLANATIONS_H
#define MAELYS_DATALOG_EXPLANATIONS_H
#include "datalog_program.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only structured views of the SAME prepared explanation lease as text.
 * No extraction or allocation on access. Text pointers borrow the live result;
 * records and step/variable IDs are local to this explanation, never persistent.
 * Reference backend only in this revision: other backends return UNSUPPORTED,
 * independently of their text explanation capabilities. No silent fallback. */
typedef struct {
    maelys_datalog_explanation_kind_t kind;
    int found, truncated;
    size_t step_count, premise_count;
    unsigned false_status, false_summary, query_origin, limit_hits;
    size_t candidate_rule_count, substitution_count, diagnostic_count, filter_cost_units;
    maelys_datalog_fact_t query;
} maelys_datalog_explanation_info_t;
typedef struct {
    size_t rule_id, premise_begin, premise_count;
    maelys_datalog_fact_t fact;
} maelys_datalog_explanation_step_view_t;
typedef struct {
    unsigned kind, origin, body_index, parent_step;
    maelys_datalog_ir_atom_t atom; /* ground fact, absence, or aggregate source */
    maelys_datalog_ir_comparison_t op;
    maelys_datalog_value_t lhs, rhs, filter_value;
    size_t filter_program_index;
    unsigned filter_kind, projected_variable;
    uint32_t aggregate_value;
} maelys_datalog_explanation_premise_view_t;
typedef struct {
    unsigned body_index, origin;
    maelys_datalog_fact_t fact;
} maelys_datalog_explanation_support_view_t;
typedef struct {
    size_t rule_id, depth;
    maelys_datalog_fact_t target;
    uint32_t bound_variable_mask;
    maelys_datalog_value_t substitution[MAELYS_DATALOG_IR_MAX_VARIABLES];
    size_t support_count;
    maelys_datalog_explanation_support_view_t supports[MAELYS_DATALOG_IR_MAX_BODY];
    unsigned obstacle_kind, obstacle_origin, body_index, unbound_term_mask;
    maelys_datalog_ir_atom_t pattern;
    maelys_datalog_ir_comparison_t op;
    maelys_datalog_value_t lhs, rhs, filter_value;
    size_t filter_program_index;
    unsigned filter_kind;
} maelys_datalog_explanation_obstacle_view_t;
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_info(
    const maelys_datalog_prepared_explanation_t *prepared, maelys_datalog_explanation_info_t *out);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_step(
    const maelys_datalog_prepared_explanation_t *prepared, size_t index, maelys_datalog_explanation_step_view_t *out);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_premise(
    const maelys_datalog_prepared_explanation_t *prepared, size_t index, maelys_datalog_explanation_premise_view_t *out);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_obstacle(
    const maelys_datalog_prepared_explanation_t *prepared, size_t index, maelys_datalog_explanation_obstacle_view_t *out);
#ifdef __cplusplus
}
#endif
#endif
