/* SPDX-License-Identifier: MPL-2.0 */
#ifndef MAELYS_DATALOG_ADVANCED_H
#define MAELYS_DATALOG_ADVANCED_H
#include "datalog_extension.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Same policy handle as inline/file loading. Sources are borrowed for this call
 * only; the compiled policy owns its content. No compatibility wrapper types. */
typedef struct {
    const char *policy_id;
    const char *src;
    size_t src_len;
} maelys_datalog_policy_bundle_entry_t;
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_manifest_text(
    const char *, size_t, const maelys_datalog_policy_bundle_entry_t *, size_t,
    unsigned flags, maelys_datalog_policy_t **, maelys_datalog_diagnostic_t *);

/* Caller-owned policy storage: fixed profile capacity (up to eight policies),
 * no allocation for the policy object and no heap fallback. Loading itself may
 * allocate in JSON parsing, contexts or extension callbacks: NOT a whole-loader
 * zero-malloc guarantee. Storage must be unused, aligned, disjoint from inputs,
 * outputs and diagnostics, and outlive the handle. Failure may overwrite it;
 * *out remains NULL. Close before reuse. free closes but never frees this arena.
 * The convenience loaders allocate exactly one policy object in addition to
 * loader/callback allocations. Sessions make their own prepared program copy. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_storage_requirements(size_t *, size_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_frontend_in(
    void *, size_t, const char *domain, const char *policy_id, const char *source, size_t,
    const maelys_datalog_frontend_t *, maelys_datalog_policy_t **, maelys_datalog_diagnostic_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_policy_load_manifest_text_in(
    void *, size_t, const char *, size_t, const maelys_datalog_policy_bundle_entry_t *, size_t,
    unsigned, maelys_datalog_policy_t **, maelys_datalog_diagnostic_t *);

/* Runs at policy loading, against a bounded candidate registry. A failure is
 * sticky even if the callback ignores it; the loader discards that candidate.
 * Registration copies declaration text; callback code must outlive all loads.
 * Exactly one of domain.predicates and installer is provided. Domains remain
 * global; contexts do not isolate them. Incompatible re-registration fails. */
typedef struct maelys_datalog_domain_builder maelys_datalog_domain_builder_t;
typedef maelys_datalog_status_t (*maelys_datalog_domain_installer_t)(maelys_datalog_domain_builder_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_domain_register_advanced(
    const maelys_datalog_domain_t *, const char *description, maelys_datalog_domain_installer_t);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_domain_builder_add(
    maelys_datalog_domain_builder_t *, const maelys_datalog_predicate_t *);

/* Compose backend/work requirements with the existing explanation storage
 * configuration. The descriptor is copied; callback code must outlive sessions.
 * Unsupported combinations fail before any session is returned. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_backend(
    maelys_datalog_session_config_t *, const maelys_datalog_backend_t *);
/* Select a registered backend, or NULL for the reference, in a sealed context.
 * The config retains
 * the context; the policy must have been compiled in that same context. Setting
 * a direct backend resets this selection, and vice versa. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_context(
    maelys_datalog_session_config_t *, maelys_datalog_context_t *, const char *backend_name);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_session_config_set_requirements(
    maelys_datalog_session_config_t *, uint64_t capabilities, uint64_t work_limit);

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
    const maelys_datalog_prepared_explanation_t *, maelys_datalog_explanation_info_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_step(
    const maelys_datalog_prepared_explanation_t *, size_t, maelys_datalog_explanation_step_view_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_premise(
    const maelys_datalog_prepared_explanation_t *, size_t, maelys_datalog_explanation_premise_view_t *);
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_prepared_explanation_obstacle(
    const maelys_datalog_prepared_explanation_t *, size_t, maelys_datalog_explanation_obstacle_view_t *);

/* Reference backend only; reports evaluations by solving, not Why-false search. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_result_filter_statistics(
    const maelys_datalog_result_t *, maelys_datalog_filter_statistics_t *);
/* Explicit facts of presence, no query side effect. Values must be 0 or 1. */
MAELYS_DATALOG_API maelys_datalog_status_t maelys_datalog_decision_from_presence(
    int allow, int reduce, int deny, maelys_datalog_decision_t *);
#ifdef __cplusplus
}
#endif
#endif
