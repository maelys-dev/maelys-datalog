#include "src/core/maelys_datalog_prepared_session.h"

#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"

#include <stdlib.h>
#include <string.h>

static int symbol_pointer_cmp(const void *lhs, const void *rhs) {
    const char *const left = *(const char *const *)lhs;
    const char *const right = *(const char *const *)rhs;
    if (!left && !right) return 0;
    if (!left) return -1;
    if (!right) return 1;
    return strcmp(left, right);
}

static maelys_result_t collect_input_symbols(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    size_t *out_count) {
    if (!session || (!facts && fact_count > 0u) || !out_count) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    size_t count = 0u;
    for (size_t i = 0u; i < fact_count; i++) {
        const maelys_datalog_input_fact_t *fact = &facts[i];
        if (!fact->predicate || fact->arity > MAELYS_DATALOG_MAX_TERMS) {
            return MAELYS_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = 0u; j < fact->arity; j++) {
            const maelys_datalog_input_term_t *term = &fact->terms[j];
            switch (term->kind) {
                case MAELYS_DATALOG_TERM_SYMBOL:
                    if (!term->as.symbol || count >= MAELYS_DATALOG_MAX_INPUT_SYMBOLS) {
                        return term->as.symbol
                            ? MAELYS_ERR_PAYLOAD_TOO_LARGE
                            : MAELYS_ERR_INVALID_ARGUMENT;
                    }
                    session->symbol_inputs[count++] = term->as.symbol;
                    break;
                case MAELYS_DATALOG_TERM_INT:
                case MAELYS_DATALOG_TERM_BOOL:
                    break;
                case MAELYS_DATALOG_TERM_VAR:
                default:
                    return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    *out_count = count;
    return MAELYS_OK;
}

static maelys_result_t intern_input_symbols(
    maelys_datalog_prepared_session_t *session,
    size_t count) {
    if (!session || count > MAELYS_DATALOG_MAX_INPUT_SYMBOLS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (count > 1u) {
        qsort(session->symbol_inputs,
              count,
              sizeof(session->symbol_inputs[0]),
              symbol_pointer_cmp);
    }
    const char *previous = NULL;
    for (size_t i = 0u; i < count; i++) {
        const char *text = session->symbol_inputs[i];
        if (previous && strcmp(previous, text) == 0) continue;
        maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
        maelys_result_t rc = maelys_datalog_symbol_intern(
            &session->working.symbols, text, strlen(text), &id);
        if (rc != MAELYS_OK) return rc;
        previous = text;
    }
    return MAELYS_OK;
}

static maelys_result_t materialize_input_fact(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *input) {
    if (!session || !input || !input->predicate ||
        input->arity > MAELYS_DATALOG_MAX_TERMS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_TERMS];
    memset(terms, 0, sizeof(terms));
    for (size_t i = 0u; i < input->arity; i++) {
        const maelys_datalog_input_term_t *source = &input->terms[i];
        terms[i].kind = source->kind;
        switch (source->kind) {
            case MAELYS_DATALOG_TERM_SYMBOL: {
                int found = 0;
                maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
                maelys_result_t rc = maelys_datalog_symbol_lookup_readonly(
                    &session->working.symbols,
                    source->as.symbol,
                    strlen(source->as.symbol),
                    &id,
                    &found);
                if (rc != MAELYS_OK) return rc;
                if (!found) return MAELYS_ERR_INTERNAL;
                terms[i].as.symbol = id;
                break;
            }
            case MAELYS_DATALOG_TERM_INT:
                terms[i].as.integer = source->as.integer;
                break;
            case MAELYS_DATALOG_TERM_BOOL:
                terms[i].as.boolean = source->as.boolean ? 1 : 0;
                break;
            case MAELYS_DATALOG_TERM_VAR:
            default:
                return MAELYS_ERR_INVALID_FIELD;
        }
    }
    return maelys_datalog_edb_add_fact(
        &session->edb, input->predicate, terms, input->arity);
}

static maelys_result_t reset_transaction_state(
    maelys_datalog_prepared_session_t *session) {
    if (!session) return MAELYS_ERR_INVALID_ARGUMENT;
    session->working.symbols = session->prepared.symbols;
    memset(session->fact_pool, 0, sizeof(session->fact_pool));
    memset(session->symbol_inputs, 0, sizeof(session->symbol_inputs));
    return maelys_datalog_edb_init(
        &session->edb,
        session->fact_pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &session->working.symbols,
        &session->working.registry);
}

static maelys_result_t reject_transaction(
    maelys_datalog_prepared_session_t *session,
    maelys_result_t rejection) {
    maelys_result_t reset = reset_transaction_state(session);
    return reset == MAELYS_OK ? rejection : reset;
}

maelys_result_t maelys_datalog_prepared_session_create(
    const maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_prepared_session_t **out_session) {
    if (out_session) *out_session = NULL;
    if (!ruleset || !out_session) return MAELYS_ERR_INVALID_ARGUMENT;
    if (!ruleset->loaded || !maelys_sha256_hex_is_lowercase(ruleset->sha256)) {
        return MAELYS_ERR_INVALID_STATE;
    }
    maelys_datalog_prepared_session_t *session = calloc(1u, sizeof(*session));
    if (!session) return MAELYS_ERR_INTERNAL;
    session->prepared = *ruleset;
    session->working = *ruleset;
    maelys_result_t rc = maelys_datalog_edb_init(
        &session->edb,
        session->fact_pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &session->working.symbols,
        &session->working.registry);
    if (rc != MAELYS_OK) {
        memset(session, 0, sizeof(*session));
        free(session);
        return rc;
    }
    *out_session = session;
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_prepared_session_destroy(
    maelys_datalog_prepared_session_t *session) {
    if (!session) return MAELYS_ERR_INVALID_ARGUMENT;
    if (session->active_result) return MAELYS_ERR_INVALID_STATE;
    memset(session, 0, sizeof(*session));
    free(session);
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_prepared_session_solve(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    maelys_datalog_solve_result_t **out_result) {
    return maelys_datalog_prepared_session_solve_ex(
        session, facts, fact_count, out_result, NULL);
}

maelys_result_t maelys_datalog_prepared_session_materialize_inputs(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count) {
    if (!session || (!facts && fact_count > 0u)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (session->active_result) return MAELYS_ERR_INVALID_STATE;
    if (fact_count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    /* Reset only mutable transaction state. The parsed rules, registry,
     * strata and prepared identity are reused without another ruleset copy. */
    maelys_result_t rc = reset_transaction_state(session);
    if (rc != MAELYS_OK) return rc;

    size_t symbol_count = 0u;
    rc = collect_input_symbols(session, facts, fact_count, &symbol_count);
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    rc = intern_input_symbols(session, symbol_count);
    memset(session->symbol_inputs, 0, sizeof(session->symbol_inputs));
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    for (size_t i = 0u; i < fact_count; i++) {
        rc = materialize_input_fact(session, &facts[i]);
        if (rc != MAELYS_OK) return reject_transaction(session, rc);
    }
    rc = maelys_datalog_edb_finalize(&session->edb);
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_prepared_session_solve_ex(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts, size_t fact_count,
    maelys_datalog_solve_result_t **out_result, maelys_datalog_solve_diagnostic_t *out_diag) {
    if (out_result) *out_result = NULL;
    if (!out_result) return MAELYS_ERR_INVALID_ARGUMENT;
    maelys_result_t rc = maelys_datalog_prepared_session_materialize_inputs(session, facts, fact_count);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_solve_once_ex(
        &session->working, &session->edb, out_result, out_diag);
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    maelys_datalog_solve_result_set_release(
        *out_result,
        session,
        maelys_datalog_prepared_session_result_released);
    session->active_result = *out_result;
    return MAELYS_OK;
}

const char *maelys_datalog_prepared_session_fingerprint(
    const maelys_datalog_prepared_session_t *session) {
    return session ? session->prepared.sha256 : NULL;
}

maelys_result_t maelys_datalog_prepared_session_lookup_symbol(
    const maelys_datalog_prepared_session_t *session,
    const char *text,
    maelys_datalog_symbol_id_t *out_id,
    int *out_found) {
    if (!session || !text || !out_id || !out_found) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (!session->active_result) return MAELYS_ERR_INVALID_STATE;
    return maelys_datalog_symbol_lookup_readonly(
        &session->working.symbols, text, strlen(text), out_id, out_found);
}

void maelys_datalog_prepared_session_result_released(
    void *owner,
    maelys_datalog_solve_result_t *result) {
    maelys_datalog_prepared_session_t *session = owner;
    if (session && session->active_result == result) {
        session->active_result = NULL;
    }
}
