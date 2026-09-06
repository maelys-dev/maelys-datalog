/* SPDX-License-Identifier: MPL-2.0 */
/* Compatibility adapter around the open reference implementation. Only this
 * adapter knows its legacy session/result layout; external backends don't. */
#include "src/compiler/maelys_datalog_program_internal.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"
#include "src/core/maelys_datalog_explanation_format.h"
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
            inputs[i].terms[j].kind = (maelys_datalog_term_kind_t)facts[i].terms[j].kind;
            switch (facts[i].terms[j].kind) {
            case MAELYS_DATALOG_VALUE_SYMBOL:
                inputs[i].terms[j].as.symbol = facts[i].terms[j].as.symbol;
                break;
            case MAELYS_DATALOG_VALUE_INTEGER:
                inputs[i].terms[j].as.integer = facts[i].terms[j].as.integer;
                break;
            case MAELYS_DATALOG_VALUE_BOOLEAN:
                inputs[i].terms[j].as.boolean = facts[i].terms[j].as.boolean;
                break;
            default:
                free(inputs);
                return MAELYS_DATALOG_STATUS_INVALID_FIELD;
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
static maelys_datalog_status_t explain(void *state, void *result_state, const char *predicate,
                                       const maelys_datalog_public_value_t *terms, size_t arity,
                                       char *text, size_t capacity, size_t *required) {
    maelys_datalog_prepared_session_t *session = state;
    maelys_datalog_solve_result_t *result = result_state;
    maelys_datalog_fact_t fact = {0};
    fact.arity = (uint8_t)arity;
    if (!maelys_datalog_predicate_registry_find(&session->working.registry, predicate, arity,
                                                &fact.predicate_id))
        return MAELYS_DATALOG_STATUS_INVALID_FIELD;
    for (size_t i = 0; i < arity; ++i) {
        fact.terms[i].kind = (maelys_datalog_term_kind_t)terms[i].kind;
        switch (terms[i].kind) {
        case MAELYS_DATALOG_VALUE_SYMBOL: {
            int found;
            maelys_result_t rc = maelys_datalog_prepared_session_lookup_symbol(
                session, terms[i].as.symbol, &fact.terms[i].as.symbol, &found);
            if (rc != MAELYS_OK)
                return (maelys_datalog_status_t)rc;
            if (!found)
                return MAELYS_DATALOG_STATUS_NOT_FOUND;
            break;
        }
        case MAELYS_DATALOG_VALUE_INTEGER:
            fact.terms[i].as.integer = terms[i].as.integer;
            break;
        case MAELYS_DATALOG_VALUE_BOOLEAN:
            fact.terms[i].as.boolean = terms[i].as.boolean ? 1 : 0;
            break;
        default:
            return MAELYS_DATALOG_STATUS_INVALID_FIELD;
        }
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
                                                         MAELYS_DATALOG_CAP_EXPLAIN_TRUE,
                                                     prepare,
                                                     solve,
                                                     explain,
                                                     destroy_result,
                                                     destroy};
    return &backend;
}
