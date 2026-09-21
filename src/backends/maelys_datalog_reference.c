/* SPDX-License-Identifier: MPL-2.0 */
/* The reference backend borrows the runtime's prepared inputs. External
 * backends consume the same program and canonical facts through the public ABI. */
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/public/maelys_datalog_values_internal.h"
#include <stdlib.h>
#include <string.h>

static maelys_datalog_status_t prepare(const maelys_datalog_program_t *program, void **out) {
    *out = program->prepared_inputs;
    return *out ? MAELYS_DATALOG_STATUS_OK : MAELYS_DATALOG_STATUS_INVALID_STATE;
}
static maelys_datalog_status_t solve(void *state, const maelys_datalog_public_fact_t *facts,
                                     size_t count, maelys_datalog_backend_output_t *output,
                                     void **out_result,
                                     maelys_datalog_public_diagnostic_t *diagnostic) {
    maelys_datalog_prepared_session_t *session = state;
    (void)facts;
    (void)count;
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag = {0};
    maelys_result_t rc =
        maelys_datalog_prepared_session_solve_materialized_ex(session, &result, &diag);
    if (rc != MAELYS_OK) {
        maelys_datalog_copy_solve_diagnostic(diagnostic, &diag);
        return (maelys_datalog_status_t)rc;
    }
    *out_result = result; /* The host cleans up even if emission fails. */
    size_t derived = 0;
    rc = maelys_datalog_solve_result_derived_fact_count(result, &derived);
    for (size_t i = 0; rc == MAELYS_OK && i < derived; ++i) {
        maelys_datalog_fact_t fact;
        maelys_datalog_public_fact_t view;
        rc = maelys_datalog_solve_result_idb_fact(result, i, &fact);
        if (rc == MAELYS_OK)
            rc = maelys_datalog_export_fact(&session->working, &fact, &view);
        if (rc == MAELYS_OK)
            rc = (maelys_result_t)maelys_datalog_backend_emit(output, &view);
    }
    return (maelys_datalog_status_t)rc;
}
static maelys_datalog_status_t explanation_storage_requirements(
    void *state, void *result, maelys_datalog_explanation_kind_t kind,
    size_t *bytes, size_t *alignment) {
    (void)state;
    if (kind == MAELYS_DATALOG_EXPLAIN_FALSE)
        return (maelys_datalog_status_t)maelys_datalog_why_false_storage_requirements(result, bytes, alignment);
    if (kind != MAELYS_DATALOG_EXPLAIN_TRUE) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    *bytes = sizeof(maelys_datalog_explanation_t);
    *alignment = _Alignof(maelys_datalog_explanation_t);
    return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t explanation_prepare(
    void *state, void *result_state, maelys_datalog_explanation_kind_t kind,
    const char *predicate, const maelys_datalog_public_value_t *terms, size_t arity,
    void *storage, size_t bytes, size_t *required) {
    maelys_datalog_prepared_session_t *session = state;
    maelys_datalog_solve_result_t *result = result_state;
    maelys_datalog_fact_t fact = {0};
    fact.arity = (uint8_t)arity;
    if (!maelys_datalog_predicate_registry_find(&session->working.registry, predicate, arity,
                                                &fact.predicate_id))
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    int found = 0;
    maelys_datalog_status_t status = maelys_datalog_resolve_public_terms(
        &session->working.symbols, terms, arity, fact.terms, &found, 0);
    if (status)
        return status;
    if (!found)
        return MAELYS_DATALOG_STATUS_NOT_FOUND;
    if (kind == MAELYS_DATALOG_EXPLAIN_FALSE) {
        const maelys_datalog_why_false_limits_t limits = {
            MAELYS_DATALOG_MAX_RULES, MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE,
            MAELYS_DATALOG_MAX_PROOF_DEPTH, MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS};
        const maelys_datalog_why_false_explanation_t *why;
        maelys_result_t rc = maelys_datalog_explain_absent_in_workspace(result, &fact, &limits, storage, bytes, &why);
        if (!rc)
            rc = maelys_datalog_format_why_false_text(&session->working, why, NULL, 0,
                                                      required);
        return (maelys_datalog_status_t)rc;
    }
    if (kind != MAELYS_DATALOG_EXPLAIN_TRUE) return MAELYS_DATALOG_STATUS_INVALID_ARGUMENT;
    if (bytes < sizeof(maelys_datalog_explanation_t)) return MAELYS_DATALOG_STATUS_PAYLOAD_TOO_LARGE;
    maelys_datalog_explanation_t *explanation = storage;
    maelys_result_t rc = maelys_datalog_explain_solved_fact(result, &fact, explanation);
    if (rc == MAELYS_OK)
        rc = maelys_datalog_format_explanation_text(&session->working, explanation, NULL, 0,
                                                    required);
    return (maelys_datalog_status_t)rc;
}
static maelys_datalog_status_t explanation_write_text(
    void *state, void *result, maelys_datalog_explanation_kind_t kind,
    const void *storage, char *text, size_t capacity) {
    (void)result;
    maelys_datalog_prepared_session_t *session = state;
    size_t required;
    if (kind == MAELYS_DATALOG_EXPLAIN_FALSE)
        return (maelys_datalog_status_t)maelys_datalog_format_why_false_text(
            &session->working, maelys_datalog_why_false_workspace_view(storage), text, capacity, &required);
    return (maelys_datalog_status_t)maelys_datalog_format_explanation_text(
        &session->working, storage, text, capacity, &required);
}
static void destroy_result(void *state, void *result) {
    (void)state;
    maelys_datalog_solve_result_free(result);
}
static void destroy(void *state) {
    (void)state; /* The runtime owns and releases the prepared session. */
}
const maelys_datalog_backend_t *maelys_datalog_backend_reference(void) {
    static const maelys_datalog_backend_t backend = {MAELYS_DATALOG_BACKEND_ABI_VERSION,
                                                     sizeof(maelys_datalog_backend_t),
                                                     "reference",
                                                     "maelys.reference.v1",
                                                     MAELYS_DATALOG_CAP_LANGUAGE |
                                                         MAELYS_DATALOG_CAP_AGGREGATES |
                                                         MAELYS_DATALOG_CAP_MIN |
                                                         MAELYS_DATALOG_CAP_MAX |
                                                         MAELYS_DATALOG_CAP_SUM |
                                                         MAELYS_DATALOG_CAP_EXPLAIN_TRUE |
                                                         MAELYS_DATALOG_CAP_EXPLAIN_FALSE,
                                                     prepare,
                                                     solve,
                                                     explanation_storage_requirements,
                                                     explanation_prepare,
                                                     explanation_write_text,
                                                     destroy_result,
                                                     destroy};
    return &backend;
}
