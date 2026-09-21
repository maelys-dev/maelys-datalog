#include "src/core/maelys_datalog_prepared_session.h"

#include "common/maelys_sha256.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "src/core/maelys_datalog_solver_internal.h"
#include "src/core/maelys_datalog_sort_internal.h"
#include "src/core/maelys_datalog_pipeline_testing.h"
#include "src/registry/maelys_datalog_modules_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void input_message(char *out, size_t capacity, const char *format, ...) {
    if (!out || !capacity) return;
    va_list args;
    va_start(args, format);
    vsnprintf(out, capacity, format, args);
    va_end(args);
}

#ifdef MAELYS_TESTING
_Thread_local maelys_datalog_pipeline_counts_t maelys_datalog_pipeline_counts;
#endif

static int symbol_pointer_cmp(const void *lhs, const void *rhs) {
    const char *const left = *(const char *const *)lhs;
    const char *const right = *(const char *const *)rhs;
    if (!left && !right) return 0;
    if (!left) return -1;
    if (!right) return 1;
    return strcmp(left, right);
}

MAELYS_DEFINE_SORT(sort_symbol_pointers, const char *, symbol_pointer_cmp)

static maelys_result_t collect_input_symbols(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    size_t *out_count, char *message, size_t message_capacity) {
    if (!session || (!facts && fact_count > 0u) || !out_count) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    size_t count = 0u;
    for (size_t i = 0u; i < fact_count; i++) {
        const maelys_datalog_input_fact_t *fact = &facts[i];
        if (!fact->predicate || fact->arity > MAELYS_DATALOG_MAX_TERMS) {
            input_message(message, message_capacity,
                          "Invalid fact at index %zu: missing predicate or arity exceeds %u.",
                          i, MAELYS_DATALOG_MAX_TERMS);
            return MAELYS_ERR_INVALID_ARGUMENT;
        }
        for (size_t j = 0u; j < fact->arity; j++) {
            const maelys_datalog_input_term_t *term = &fact->terms[j];
            switch (term->kind) {
                case MAELYS_DATALOG_TERM_SYMBOL:
                    if (!term->as.symbol || count >= MAELYS_DATALOG_MAX_INPUT_SYMBOLS) {
                        input_message(message, message_capacity,
                                      "Invalid fact at index %zu (%.63s), term %zu: %s.",
                                      i, fact->predicate, j,
                                      term->as.symbol ? "input symbol capacity exceeded" : "NULL symbol");
                        return term->as.symbol
                            ? MAELYS_ERR_PAYLOAD_TOO_LARGE
                            : MAELYS_ERR_INVALID_ARGUMENT;
                    }
                    if (strnlen(term->as.symbol, MAELYS_DATALOG_MAX_STRING_BYTES + 1u) >
                        MAELYS_DATALOG_MAX_STRING_BYTES) {
                        input_message(message, message_capacity,
                                      "Invalid fact at index %zu (%.63s), term %zu: symbol exceeds %u bytes.",
                                      i, fact->predicate, j, MAELYS_DATALOG_MAX_STRING_BYTES);
                        return MAELYS_ERR_INVALID_ARGUMENT;
                    }
                    session->symbol_inputs[count++] = term->as.symbol;
                    break;
                case MAELYS_DATALOG_TERM_INT:
                case MAELYS_DATALOG_TERM_BOOL:
                    break;
                case MAELYS_DATALOG_TERM_VAR:
                default:
                    input_message(message, message_capacity,
                                  "Invalid fact at index %zu (%.63s), term %zu: unsupported value kind %d.",
                                  i, fact->predicate, j, (int)term->kind);
                    return MAELYS_ERR_INVALID_FIELD;
            }
        }
    }
    *out_count = count;
    return MAELYS_OK;
}

static maelys_result_t intern_input_symbols(
    maelys_datalog_prepared_session_t *session,
    size_t count, const maelys_datalog_input_fact_t *facts, size_t fact_count,
    char *message, size_t message_capacity) {
    if (!session || count > MAELYS_DATALOG_MAX_INPUT_SYMBOLS) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (count > 1u) {
        sort_symbol_pointers(session->symbol_inputs, count);
    }
    const char *previous = NULL;
    for (size_t i = 0u; i < count; i++) {
        const char *text = session->symbol_inputs[i];
        if (previous && strcmp(previous, text) == 0) continue;
        maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
        maelys_result_t rc = maelys_datalog_symbol_intern(
            &session->working.symbols, text, strlen(text), &id);
        if (rc != MAELYS_OK) {
            /* Sorting makes IDs deterministic; locate the original occurrence
             * only on failure, without exposing symbol contents in diagnostics. */
            const char *reason = "symbol interning failed";
            size_t bound = 0u;
            if (session->working.symbols.count >= MAELYS_DATALOG_MAX_SYMBOLS) {
                reason = "symbol count limit";
                bound = MAELYS_DATALOG_MAX_SYMBOLS;
            } else if (strlen(text) + 1u >
                       sizeof(session->working.symbols.storage) - session->working.symbols.used) {
                reason = "symbol storage byte limit";
                bound = sizeof(session->working.symbols.storage);
            }
            for (size_t f = 0u; f < fact_count; ++f) {
                for (size_t t = 0u; t < facts[f].arity; ++t) {
                    if (facts[f].terms[t].kind == MAELYS_DATALOG_TERM_SYMBOL &&
                        strcmp(facts[f].terms[t].as.symbol, text) == 0) {
                        input_message(message, message_capacity,
                                      "Invalid fact at index %zu (%.63s), term %zu: %s (%zu).",
                                      f, facts[f].predicate, t, reason, bound);
                        return rc;
                    }
                }
            }
            return rc;
        }
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
    return maelys_datalog_edb_add_fact_indexed(
        &session->edb, input->predicate, terms, input->arity, &session->fact_index);
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
    session->result_workspace = maelys_datalog_solve_workspace_create();
    if (!session->result_workspace) { free(session); return MAELYS_ERR_INTERNAL; }
    session->prepared = *ruleset;
    session->working = *ruleset;
    maelys_result_t rc = maelys_datalog_edb_init(
        &session->edb,
        session->fact_pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &session->working.symbols,
        &session->working.registry);
    if (rc != MAELYS_OK) {
        maelys_datalog_solve_workspace_destroy(session->result_workspace);
        memset(session, 0, sizeof(*session));
        free(session);
        return rc;
    }
    maelys_datalog_context_retain(ruleset->modules);
    *out_session = session;
    MAELYS_DATALOG_COUNT_PIPELINE(preparations);
    return MAELYS_OK;
}

maelys_result_t maelys_datalog_prepared_session_destroy(
    maelys_datalog_prepared_session_t *session) {
    if (!session) return MAELYS_ERR_INVALID_ARGUMENT;
    if (session->active_result) return MAELYS_ERR_INVALID_STATE;
    maelys_datalog_context_release(session->prepared.modules);
    maelys_datalog_solve_workspace_destroy(session->result_workspace);
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

static void explain_fact_rejection(
    const maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *fact, size_t index,
    char *message, size_t message_capacity) {
    maelys_datalog_predicate_id_t pid;
    const maelys_datalog_predicate_registry_t *registry = &session->working.registry;
    if (!maelys_datalog_predicate_registry_find(registry, fact->predicate, fact->arity, &pid)) {
        for (size_t i = 0u; i < registry->count; ++i) {
            if (strcmp(registry->defs[i].name, fact->predicate) == 0) {
                input_message(message, message_capacity,
                              "Invalid fact at index %zu: %.63s expects %zu arguments, received %zu.",
                              index, fact->predicate, registry->defs[i].arity, fact->arity);
                return;
            }
        }
        input_message(message, message_capacity,
                      "Invalid fact at index %zu: unknown predicate %.63s/%zu.",
                      index, fact->predicate, fact->arity);
        return;
    }
    const maelys_datalog_predicate_def_t *def = maelys_datalog_predicate_registry_get(registry, pid);
    if (def->kind_flags & MAELYS_DATALOG_PRED_KIND_POLICY_FACT) {
        input_message(message, message_capacity,
                      "Invalid fact at index %zu: %.63s is a policy-fact predicate; runtime input is forbidden.",
                      index, fact->predicate);
    } else if (!(def->kind_flags & MAELYS_DATALOG_PRED_KIND_EDB)) {
        input_message(message, message_capacity,
                      "Invalid fact at index %zu: %.63s is not an EDB predicate.", index, fact->predicate);
    } else if (session->edb.fact_count >= MAELYS_DATALOG_MAX_EDB_FACTS) {
        input_message(message, message_capacity,
                      "Invalid fact at index %zu (%.63s): EDB fact limit %u reached.",
                      index, fact->predicate, MAELYS_DATALOG_MAX_EDB_FACTS);
    } else if (session->edb.facts_per_pred[pid] >= MAELYS_DATALOG_MAX_FACTS_PER_PRED) {
        input_message(message, message_capacity,
                      "Invalid fact at index %zu (%.63s): per-predicate fact limit %u reached.",
                      index, fact->predicate, MAELYS_DATALOG_MAX_FACTS_PER_PRED);
    } else {
        input_message(message, message_capacity,
                      "Invalid fact at index %zu (%.63s): input materialization failed.", index, fact->predicate);
    }
}

maelys_result_t maelys_datalog_prepared_session_materialize_inputs(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count) {
    return maelys_datalog_prepared_session_materialize_inputs_diagnosed(
        session, facts, fact_count, NULL, 0u);
}

maelys_result_t maelys_datalog_prepared_session_materialize_inputs_diagnosed(
    maelys_datalog_prepared_session_t *session,
    const maelys_datalog_input_fact_t *facts, size_t fact_count,
    char *message, size_t message_capacity) {
    if (message && message_capacity) message[0] = '\0';
    if (!session || (!facts && fact_count > 0u)) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    if (session->active_result) return MAELYS_ERR_INVALID_STATE;
    if (fact_count > MAELYS_DATALOG_MAX_EDB_FACTS) {
        input_message(message, message_capacity,
                      "Input batch contains %zu facts; EDB fact limit is %u (before deduplication).",
                      fact_count, MAELYS_DATALOG_MAX_EDB_FACTS);
        return MAELYS_ERR_PAYLOAD_TOO_LARGE;
    }

    MAELYS_DATALOG_COUNT_PIPELINE(materializations);
    /* Reset only mutable transaction state. The parsed rules, registry,
     * strata and prepared identity are reused without another ruleset copy. */
    maelys_result_t rc = reset_transaction_state(session);
    if (rc != MAELYS_OK) return rc;

    size_t symbol_count = 0u;
    rc = collect_input_symbols(session, facts, fact_count, &symbol_count, message, message_capacity);
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    rc = intern_input_symbols(session, symbol_count, facts, fact_count, message, message_capacity);
    /* Drop borrowed pointers and initialize the overlapping insertion index. */
    memset(session->symbol_inputs, 0, sizeof(session->symbol_inputs));
    if (rc != MAELYS_OK) return reject_transaction(session, rc);
    for (size_t i = 0u; i < fact_count; i++) {
        rc = materialize_input_fact(session, &facts[i]);
        if (rc != MAELYS_OK) {
            explain_fact_rejection(session, &facts[i], i, message, message_capacity);
            return reject_transaction(session, rc);
        }
    }
    rc = maelys_datalog_edb_finalize(&session->edb);
    if (rc != MAELYS_OK) {
        input_message(message, message_capacity, "Input batch finalization failed (status %d).", (int)rc);
        return reject_transaction(session, rc);
    }
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
    return maelys_datalog_prepared_session_solve_materialized_ex(session, out_result, out_diag);
}

maelys_result_t maelys_datalog_prepared_session_solve_materialized_ex(
    maelys_datalog_prepared_session_t *session,
    maelys_datalog_solve_result_t **out_result, maelys_datalog_solve_diagnostic_t *out_diag) {
    if (out_result) *out_result = NULL;
    if (!session || !out_result) return MAELYS_ERR_INVALID_ARGUMENT;
    if (session->active_result || !session->edb.immutable) return MAELYS_ERR_INVALID_STATE;
    maelys_result_t rc = maelys_datalog_solve_reusing_workspace(
        &session->working, &session->edb, session->result_workspace, out_result, out_diag);
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
