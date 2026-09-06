/* SPDX-License-Identifier: MPL-2.0 */
/* Compatibility adapter around the open reference implementation. Only this
 * adapter knows its legacy session/result layout; external backends don't. */
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/public/maelys_datalog_values_internal.h"
#include <stdlib.h>
#include <string.h>

static maelys_datalog_status_t prepare(const maelys_datalog_program_t *program, void **out) {
    maelys_datalog_prepared_session_t *session = NULL;
    maelys_result_t rc = maelys_datalog_prepared_session_create(program->ruleset, &session);
    *out = session;
    return (maelys_datalog_status_t)rc;
}
static maelys_datalog_status_t solve(void *state, const maelys_datalog_public_fact_t *facts,
                                     size_t count, maelys_datalog_backend_output_t *output,
                                     void **out_result,
                                     maelys_datalog_public_diagnostic_t *diagnostic) {
    maelys_datalog_prepared_session_t *session = state;
    maelys_datalog_input_fact_t *inputs = count ? calloc(count, sizeof(*inputs)) : NULL;
    if (count && !inputs)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    for (size_t i = 0; i < count; ++i) {
        inputs[i].predicate = facts[i].predicate;
        inputs[i].arity = facts[i].arity;
        for (size_t j = 0; j < facts[i].arity; ++j) {
            maelys_datalog_status_t rc =
                maelys_datalog_import_public_value(&facts[i].terms[j], 0, &inputs[i].terms[j]);
            if (rc) {
                free(inputs);
                return rc;
            }
        }
    }
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag = {0};
    maelys_result_t rc =
        maelys_datalog_prepared_session_solve_ex(session, inputs, count, &result, &diag);
    free(inputs);
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
static maelys_datalog_status_t explain_result(void *state, void *result_state,
                                              const char *predicate,
                                              const maelys_datalog_public_value_t *terms,
                                              size_t arity, char *text, size_t capacity,
                                              size_t *required, int absent) {
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
    if (absent) {
        const maelys_datalog_why_false_limits_t limits = {
            MAELYS_DATALOG_MAX_RULES, MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE,
            MAELYS_DATALOG_MAX_PROOF_DEPTH, MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS};
        maelys_datalog_why_false_explanation_t *why = calloc(1, sizeof(*why));
        if (!why)
            return MAELYS_DATALOG_STATUS_INTERNAL;
        maelys_result_t rc = maelys_datalog_explain_absent_solved_fact(result, &fact, &limits, why);
        if (!rc)
            rc = maelys_datalog_format_why_false_text(&session->working, why, text, capacity,
                                                      required);
        free(why);
        return (maelys_datalog_status_t)rc;
    }
    maelys_datalog_explanation_t *explanation = calloc(1u, sizeof(*explanation));
    if (!explanation)
        return MAELYS_DATALOG_STATUS_INTERNAL;
    maelys_result_t rc = maelys_datalog_explain_solved_fact(result, &fact, explanation);
    if (rc == MAELYS_OK)
        rc = maelys_datalog_format_explanation_text(&session->working, explanation, text, capacity,
                                                    required);
    free(explanation);
    return (maelys_datalog_status_t)rc;
}
static maelys_datalog_status_t explain_true(void *state, void *result, const char *predicate,
                                            const maelys_datalog_public_value_t *terms,
                                            size_t arity, char *text, size_t capacity,
                                            size_t *required) {
    return explain_result(state, result, predicate, terms, arity, text, capacity, required, 0);
}
static maelys_datalog_status_t explain_false(void *state, void *result, const char *predicate,
                                             const maelys_datalog_public_value_t *terms,
                                             size_t arity, char *text, size_t capacity,
                                             size_t *required) {
    return explain_result(state, result, predicate, terms, arity, text, capacity, required, 1);
}
static void destroy_result(void *state, void *result) {
    (void)state;
    maelys_datalog_solve_result_free(result);
}
static void destroy(void *state) {
    if (state)
        (void)maelys_datalog_prepared_session_destroy(state);
}
const maelys_datalog_backend_t *maelys_datalog_backend_reference(void) {
    static const maelys_datalog_backend_t backend = {MAELYS_DATALOG_BACKEND_ABI_VERSION,
                                                     sizeof(maelys_datalog_backend_t),
                                                     "reference",
                                                     "maelys.reference.v1",
                                                     MAELYS_DATALOG_CAP_LANGUAGE |
                                                         MAELYS_DATALOG_CAP_EXPLAIN_TRUE |
                                                         MAELYS_DATALOG_CAP_EXPLAIN_FALSE,
                                                     prepare,
                                                     solve,
                                                     explain_true,
                                                     explain_false,
                                                     destroy_result,
                                                     destroy};
    return &backend;
}
