#include "include/maelys_datalog.h"
#include "src/core/maelys_datalog_prepared_session_internal.h"
#include "tests/helpers/test_framework.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

maelys_result_t maelys_datalog_test_solve_result_idb_facts(
    const maelys_datalog_solve_result_t *result,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count);
maelys_result_t maelys_datalog_test_solve_result_idb_proof_indices(
    const maelys_datalog_solve_result_t *result,
    const uint16_t **out_indices,
    size_t *out_count);
maelys_result_t maelys_datalog_test_solve_result_edb_slice(
    const maelys_datalog_solve_result_t *result,
    maelys_datalog_predicate_id_t predicate_id,
    const maelys_datalog_fact_t **out_facts,
    size_t *out_count);

static const char k_fingerprint[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static maelys_result_t make_ruleset(maelys_datalog_ruleset_t *ruleset) {
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = maelys_datalog_ruleset_init(
        ruleset, "prepared.session", "authorization", k_fingerprint, 1);
    if (rc != MAELYS_OK) return rc;
    static const maelys_datalog_predicate_def_t defs[] = {
        {"member", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"admin", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"quota", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"enabled", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"allow", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0u; i < sizeof(defs) / sizeof(defs[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_domain(
            &ruleset->registry, defs[i].name, defs[i].arity, defs[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    ruleset->negation_supported = 1;
    const char *source =
        "allow(U) :- member(U, G), admin(G), not(blocked(U)).";
    return maelys_datalog_parse_ruleset(ruleset, source, strlen(source));
}

static maelys_result_t make_symbol_ruleset(maelys_datalog_ruleset_t *ruleset) {
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = maelys_datalog_ruleset_init(
        ruleset, "prepared.symbols", "authorization", k_fingerprint, 1);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry, "member", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry,
        "allow",
        1u,
        MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_atom(
        &ruleset->registry, "policy-team");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    const char source[] = "allow(U) :- member(U, \"policy-team\").";
    return maelys_datalog_parse_ruleset(ruleset, source, strlen(source));
}

static maelys_datalog_input_term_t symbol_term(const char *text) {
    maelys_datalog_input_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_SYMBOL;
    term.as.symbol = text;
    return term;
}

static maelys_datalog_input_term_t integer_term(long long value) {
    maelys_datalog_input_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_INT;
    term.as.integer = value;
    return term;
}

static maelys_datalog_input_term_t boolean_term(int value) {
    maelys_datalog_input_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_BOOL;
    term.as.boolean = value;
    return term;
}

static maelys_datalog_input_fact_t unary_fact(
    const char *predicate,
    const char *value) {
    maelys_datalog_input_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = predicate;
    fact.arity = 1u;
    fact.terms[0] = symbol_term(value);
    return fact;
}

static maelys_datalog_input_fact_t binary_fact(
    const char *predicate,
    const char *left,
    const char *right) {
    maelys_datalog_input_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = predicate;
    fact.arity = 2u;
    fact.terms[0] = symbol_term(left);
    fact.terms[1] = symbol_term(right);
    return fact;
}

static void authorization_facts(
    maelys_datalog_input_fact_t out[4],
    int reverse) {
    maelys_datalog_input_fact_t canonical[4];
    canonical[0] = binary_fact("member", "alice", "team");
    canonical[1] = unary_fact("admin", "team");
    canonical[2] = binary_fact("member", "bob", "team");
    canonical[3] = unary_fact("blocked", "bob");
    for (size_t i = 0u; i < 4u; i++) {
        out[i] = reverse ? canonical[3u - i] : canonical[i];
    }
}

static int results_byte_identical(
    const maelys_datalog_solve_result_t *lhs,
    const maelys_datalog_solve_result_t *rhs) {
    const maelys_datalog_fact_t *lhs_facts = NULL;
    const maelys_datalog_fact_t *rhs_facts = NULL;
    size_t lhs_count = 0u;
    size_t rhs_count = 0u;
    if (maelys_datalog_test_solve_result_idb_facts(
            lhs, &lhs_facts, &lhs_count) != MAELYS_OK ||
        maelys_datalog_test_solve_result_idb_facts(
            rhs, &rhs_facts, &rhs_count) != MAELYS_OK ||
        lhs_count != rhs_count ||
        (lhs_count > 0u &&
         memcmp(lhs_facts, rhs_facts, lhs_count * sizeof(lhs_facts[0])) != 0)) {
        return 0;
    }

    const uint16_t *lhs_indices = NULL;
    const uint16_t *rhs_indices = NULL;
    size_t lhs_index_count = 0u;
    size_t rhs_index_count = 0u;
    if (maelys_datalog_test_solve_result_idb_proof_indices(
            lhs, &lhs_indices, &lhs_index_count) != MAELYS_OK ||
        maelys_datalog_test_solve_result_idb_proof_indices(
            rhs, &rhs_indices, &rhs_index_count) != MAELYS_OK ||
        lhs_index_count != rhs_index_count ||
        (lhs_index_count > 0u &&
         memcmp(lhs_indices,
                rhs_indices,
                lhs_index_count * sizeof(lhs_indices[0])) != 0)) {
        return 0;
    }

    const maelys_datalog_proof_tree_t *lhs_proof =
        maelys_datalog_solve_result_proof(lhs);
    const maelys_datalog_proof_tree_t *rhs_proof =
        maelys_datalog_solve_result_proof(rhs);
    return lhs_proof && rhs_proof &&
        memcmp(lhs_proof, rhs_proof, sizeof(*lhs_proof)) == 0;
}

static int explanations_byte_identical(
    const maelys_datalog_ruleset_t *format_ruleset,
    const maelys_datalog_solve_result_t *lhs,
    const maelys_datalog_solve_result_t *rhs) {
    maelys_datalog_fact_t lhs_fact;
    maelys_datalog_fact_t rhs_fact;
    size_t lhs_count = 0u;
    size_t rhs_count = 0u;
    if (maelys_datalog_solve_result_enumerate_predicate_facts(
            lhs, "allow", 1u, &lhs_fact, 1u, &lhs_count) != MAELYS_OK ||
        maelys_datalog_solve_result_enumerate_predicate_facts(
            rhs, "allow", 1u, &rhs_fact, 1u, &rhs_count) != MAELYS_OK ||
        lhs_count != 1u || rhs_count != 1u ||
        memcmp(&lhs_fact, &rhs_fact, sizeof(lhs_fact)) != 0) {
        return 0;
    }
    maelys_datalog_explanation_t *lhs_explanation =
        calloc(1u, sizeof(*lhs_explanation));
    maelys_datalog_explanation_t *rhs_explanation =
        calloc(1u, sizeof(*rhs_explanation));
    if (!lhs_explanation || !rhs_explanation) {
        free(lhs_explanation);
        free(rhs_explanation);
        return 0;
    }
    int equal =
        maelys_datalog_explain_solved_fact(
            lhs, &lhs_fact, lhs_explanation) == MAELYS_OK &&
        maelys_datalog_explain_solved_fact(
            rhs, &rhs_fact, rhs_explanation) == MAELYS_OK &&
        memcmp(lhs_explanation,
               rhs_explanation,
               sizeof(*lhs_explanation)) == 0;
    size_t lhs_required = 0u;
    size_t rhs_required = 0u;
    if (equal) {
        equal =
            maelys_datalog_format_explanation_text(
                format_ruleset,
                lhs_explanation,
                NULL,
                0u,
                &lhs_required) == MAELYS_OK &&
            maelys_datalog_format_explanation_text(
                format_ruleset,
                rhs_explanation,
                NULL,
                0u,
                &rhs_required) == MAELYS_OK &&
            lhs_required == rhs_required;
    }
    char *lhs_text = NULL;
    char *rhs_text = NULL;
    if (equal) {
        lhs_text = malloc(lhs_required + 1u);
        rhs_text = malloc(rhs_required + 1u);
        equal = lhs_text && rhs_text &&
            maelys_datalog_format_explanation_text(
                format_ruleset,
                lhs_explanation,
                lhs_text,
                lhs_required + 1u,
                &lhs_required) == MAELYS_OK &&
            maelys_datalog_format_explanation_text(
                format_ruleset,
                rhs_explanation,
                rhs_text,
                rhs_required + 1u,
                &rhs_required) == MAELYS_OK &&
            memcmp(lhs_text, rhs_text, lhs_required + 1u) == 0;
    }
    free(lhs_text);
    free(rhs_text);
    free(lhs_explanation);
    free(rhs_explanation);
    return equal;
}

static maelys_result_t solve_fresh_canonical(
    const maelys_datalog_ruleset_t *source,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS],
    maelys_datalog_solve_result_t **out_result) {
    *ruleset = *source;
    static const char *const symbols[] = {"alice", "bob", "team"};
    for (size_t i = 0u; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        maelys_datalog_symbol_id_t id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
        maelys_result_t rc = maelys_datalog_symbol_intern(
            &ruleset->symbols, symbols[i], strlen(symbols[i]), &id);
        if (rc != MAELYS_OK) return rc;
    }
    maelys_datalog_edb_t edb;
    maelys_result_t rc = maelys_datalog_edb_init(
        &edb,
        fact_pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &ruleset->symbols,
        &ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    for (size_t i = 0u; i < fact_count; i++) {
        maelys_datalog_term_t terms[MAELYS_DATALOG_MAX_TERMS];
        memset(terms, 0, sizeof(terms));
        for (size_t j = 0u; j < facts[i].arity; j++) {
            terms[j].kind = facts[i].terms[j].kind;
            if (terms[j].kind == MAELYS_DATALOG_TERM_SYMBOL) {
                int found = 0;
                rc = maelys_datalog_symbol_lookup_readonly(
                    &ruleset->symbols,
                    facts[i].terms[j].as.symbol,
                    strlen(facts[i].terms[j].as.symbol),
                    &terms[j].as.symbol,
                    &found);
                if (rc != MAELYS_OK || !found) {
                    return rc != MAELYS_OK ? rc : MAELYS_ERR_INTERNAL;
                }
            } else if (terms[j].kind == MAELYS_DATALOG_TERM_INT) {
                terms[j].as.integer = facts[i].terms[j].as.integer;
            } else if (terms[j].kind == MAELYS_DATALOG_TERM_BOOL) {
                terms[j].as.boolean = facts[i].terms[j].as.boolean ? 1 : 0;
            } else {
                return MAELYS_ERR_INVALID_FIELD;
            }
        }
        rc = maelys_datalog_edb_add_fact(
            &edb, facts[i].predicate, terms, facts[i].arity);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_edb_finalize(&edb);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_solve_once(ruleset, &edb, out_result);
}

static int test_order_independent_results_and_why_true(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *first = NULL;
    maelys_datalog_prepared_session_t *second = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &second), "%d");
    maelys_datalog_input_fact_t forward[4], reverse[4];
    authorization_facts(forward, 0);
    authorization_facts(reverse, 1);
    maelys_datalog_solve_result_t *first_result = NULL;
    maelys_datalog_solve_result_t *second_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          first, forward, 4u, &first_result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          second, reverse, 4u, &second_result), "%d");
    maelys_datalog_ruleset_t oracle_ruleset;
    maelys_datalog_fact_t oracle_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_solve_result_t *oracle_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(
                          &ruleset,
                          forward,
                          4u,
                          &oracle_ruleset,
                          oracle_pool,
                          &oracle_result), "%d");
    TEST_ASSERT_TRUE(results_byte_identical(first_result, second_result));
    TEST_ASSERT_TRUE(results_byte_identical(first_result, oracle_result));
    TEST_ASSERT_TRUE(explanations_byte_identical(
        &oracle_ruleset, first_result, second_result));
    TEST_ASSERT_TRUE(explanations_byte_identical(
        &oracle_ruleset, first_result, oracle_result));
    static const char *const symbols[] = {"alice", "bob", "team"};
    for (size_t i = 0u; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
        maelys_datalog_symbol_id_t first_id = 0u, second_id = 0u;
        int first_found = 0, second_found = 0;
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_prepared_session_lookup_symbol(
                              first, symbols[i], &first_id, &first_found), "%d");
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_prepared_session_lookup_symbol(
                              second, symbols[i], &second_id, &second_found), "%d");
        TEST_ASSERT_TRUE(first_found && second_found);
        TEST_ASSERT_EQUAL(first_id, second_id, "%u");
    }
    maelys_datalog_solve_result_free(oracle_result);
    maelys_datalog_solve_result_free(first_result);
    maelys_datalog_solve_result_free(second_result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(second), "%d");
    TEST_END();
}

static int test_matches_fresh_full_solve_oracle(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t source;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&source), "%d");
    maelys_datalog_input_fact_t facts[4];
    authorization_facts(facts, 1);
    maelys_datalog_prepared_session_t *session = NULL;
    maelys_datalog_solve_result_t *prepared_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&source, &session), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 4u, &prepared_result), "%d");

    maelys_datalog_ruleset_t fresh_ruleset;
    maelys_datalog_fact_t fresh_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_solve_result_t *fresh_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(
                          &source, facts, 4u, &fresh_ruleset, fresh_pool, &fresh_result), "%d");
    TEST_ASSERT_TRUE(results_byte_identical(prepared_result, fresh_result));
    TEST_ASSERT_TRUE(explanations_byte_identical(
        &fresh_ruleset, prepared_result, fresh_result));

    maelys_datalog_solve_result_free(prepared_result);
    maelys_datalog_solve_result_free(fresh_result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_reset_discards_prior_runtime_symbols(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");
    maelys_datalog_input_fact_t first_facts[2] = {
        binary_fact("member", "alice", "team"),
        unary_fact("admin", "team"),
    };
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, first_facts, 2u, &result), "%d");
    maelys_datalog_symbol_id_t id = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alice", &id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    maelys_datalog_solve_result_free(result);

    maelys_datalog_input_fact_t second_facts[2] = {
        binary_fact("member", "charlie", "team"),
        unary_fact("admin", "team"),
    };
    result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, second_facts, 2u, &result), "%d");
    found = 1;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alice", &id, &found), "%d");
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "charlie", &id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_fingerprint_snapshot_and_result_lease(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");
    memset(&ruleset, 0, sizeof(ruleset));
    TEST_ASSERT_EQUAL_STRING(
        k_fingerprint, maelys_datalog_prepared_session_fingerprint(session));
    maelys_datalog_input_fact_t facts[2] = {
        unary_fact("admin", "team"),
        binary_fact("member", "alice", "team"),
    };
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 2u, &result), "%d");
    maelys_datalog_solve_result_t *second = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 2u, &second), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");

    maelys_datalog_ruleset_t unset;
    memset(&unset, 0, sizeof(unset));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_ruleset_init(
                          &unset, "unset", "test", "unset", 1), "%d");
    session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_prepared_session_create(&unset, &session), "%d");
    TEST_ASSERT_NULL(session);

    maelys_datalog_ruleset_t uppercase;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&uppercase), "%d");
    uppercase.sha256[0] = 'A';
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_prepared_session_create(&uppercase, &session), "%d");
    TEST_ASSERT_NULL(session);
    TEST_END();
}

static int test_argument_refusals_clear_outputs_and_preserve_session(void) {
    TEST_BEGIN();
    maelys_datalog_prepared_session_t *session =
        (maelys_datalog_prepared_session_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_prepared_session_create(NULL, &session), "%d");
    TEST_ASSERT_NULL(session);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_prepared_session_destroy(NULL), "%d");
    TEST_ASSERT_NULL(maelys_datalog_prepared_session_fingerprint(NULL));

    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");
    maelys_datalog_solve_result_t *result =
        (maelys_datalog_solve_result_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_prepared_session_solve(
                          NULL, NULL, 0u, &result), "%d");
    TEST_ASSERT_NULL(result);
    result = (maelys_datalog_solve_result_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_prepared_session_solve(
                          session, NULL, 1u, &result), "%d");
    TEST_ASSERT_NULL(result);

    maelys_datalog_input_fact_t invalid = unary_fact("admin", "team");
    invalid.arity = MAELYS_DATALOG_MAX_TERMS + 1u;
    result = (maelys_datalog_solve_result_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_prepared_session_solve(
                          session, &invalid, 1u, &result), "%d");
    TEST_ASSERT_NULL(result);
    result = (maelys_datalog_solve_result_t *)(uintptr_t)1u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_prepared_session_solve(
                          session,
                          &invalid,
                          MAELYS_DATALOG_MAX_EDB_FACTS + 1u,
                          &result), "%d");
    TEST_ASSERT_NULL(result);

    maelys_datalog_symbol_id_t id = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "team", &id, &found), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, NULL, 0u, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_refused_input_leaves_session_retryable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");

    maelys_datalog_input_fact_t invalid =
        unary_fact("unknown_predicate", "orphan");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_prepared_session_solve(
                          session, &invalid, 1u, &result), "%d");
    TEST_ASSERT_NULL(result);

    maelys_datalog_symbol_id_t id = 0u;
    int found = 1;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_lookup_readonly(
                          &session->working.symbols,
                          "orphan",
                          strlen("orphan"),
                          &id,
                          &found), "%d");
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL(session->prepared.symbols.count,
                      session->working.symbols.count,
                      "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, session->edb.fact_count, "%zu");
    for (size_t i = 0u; i < sizeof(session->fact_pool); i++) {
        TEST_ASSERT_EQUAL(0,
                          ((const unsigned char *)session->fact_pool)[i],
                          "%d");
    }

    maelys_datalog_input_fact_t valid[2] = {
        binary_fact("member", "alice", "team"),
        unary_fact("admin", "team"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, valid, 2u, &result), "%d");
    id = 0u;
    found = 1;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "orphan", &id, &found), "%d");
    TEST_ASSERT_FALSE(found);

    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_typed_terms_are_materialized_exactly(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");

    maelys_datalog_input_fact_t facts[6];
    authorization_facts(facts, 0);
    memset(&facts[4], 0, sizeof(facts[4]));
    facts[4].predicate = "quota";
    facts[4].arity = 2u;
    facts[4].terms[0] = symbol_term("alice");
    facts[4].terms[1] = integer_term(7);
    memset(&facts[5], 0, sizeof(facts[5]));
    facts[5].predicate = "enabled";
    facts[5].arity = 2u;
    facts[5].terms[0] = symbol_term("alice");
    facts[5].terms[1] = boolean_term(9);

    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 6u, &result), "%d");

    maelys_datalog_predicate_id_t quota_id = 0u;
    maelys_datalog_predicate_id_t enabled_id = 0u;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(
        &ruleset.registry, "quota", 2u, &quota_id));
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(
        &ruleset.registry, "enabled", 2u, &enabled_id));
    const maelys_datalog_fact_t *quota = NULL;
    const maelys_datalog_fact_t *enabled = NULL;
    size_t quota_count = 0u;
    size_t enabled_count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_edb_slice(
                          result, quota_id, &quota, &quota_count), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_edb_slice(
                          result, enabled_id, &enabled, &enabled_count), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, quota_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)1u, enabled_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TERM_INT, quota[0].terms[1].kind, "%d");
    TEST_ASSERT_EQUAL((long long)7, quota[0].terms[1].as.integer, "%lld");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TERM_BOOL, enabled[0].terms[1].kind, "%d");
    TEST_ASSERT_EQUAL(1, enabled[0].terms[1].as.boolean, "%d");

    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static char *owned_text(const char *source) {
    const size_t length = strlen(source);
    char *copy = malloc(length + 1u);
    if (copy) memcpy(copy, source, length + 1u);
    return copy;
}

static int test_input_text_is_borrowed_only_during_solve(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");

    char *member = owned_text("member");
    char *admin = owned_text("admin");
    char *alice = owned_text("alice");
    char *team_left = owned_text("team");
    char *team_right = owned_text("team");
    TEST_ASSERT_NOT_NULL(member);
    TEST_ASSERT_NOT_NULL(admin);
    TEST_ASSERT_NOT_NULL(alice);
    TEST_ASSERT_NOT_NULL(team_left);
    TEST_ASSERT_NOT_NULL(team_right);
    maelys_datalog_input_fact_t facts[2] = {
        binary_fact(member, alice, team_left),
        unary_fact(admin, team_right),
    };
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 2u, &result), "%d");
    free(member);
    free(admin);
    free(alice);
    free(team_left);
    free(team_right);

    maelys_datalog_symbol_id_t alice_id = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alice", &alice_id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    maelys_datalog_term_t allow_term;
    memset(&allow_term, 0, sizeof(allow_term));
    allow_term.kind = MAELYS_DATALOG_TERM_SYMBOL;
    allow_term.as.symbol = alice_id;
    bool allowed = false;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_query_solved_ground_fact(
                          result, "allow", &allow_term, 1u, &allowed), "%d");
    TEST_ASSERT_TRUE(allowed);

    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_all_permutations_and_aba_preserve_oracle(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t source;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&source), "%d");
    maelys_datalog_input_fact_t canonical[4];
    authorization_facts(canonical, 0);

    maelys_datalog_ruleset_t oracle_ruleset;
    maelys_datalog_fact_t oracle_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_solve_result_t *oracle_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(
                          &source,
                          canonical,
                          4u,
                          &oracle_ruleset,
                          oracle_pool,
                          &oracle_result), "%d");

    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&source, &session), "%d");
    size_t permutation_count = 0u;
    for (size_t a = 0u; a < 4u; a++) {
        for (size_t b = 0u; b < 4u; b++) {
            if (b == a) continue;
            for (size_t c = 0u; c < 4u; c++) {
                if (c == a || c == b) continue;
                for (size_t d = 0u; d < 4u; d++) {
                    if (d == a || d == b || d == c) continue;
                    const size_t order[4] = {a, b, c, d};
                    maelys_datalog_input_fact_t permuted[4];
                    for (size_t i = 0u; i < 4u; i++) {
                        permuted[i] = canonical[order[i]];
                    }
                    maelys_datalog_solve_result_t *result = NULL;
                    TEST_ASSERT_EQUAL(MAELYS_OK,
                                      maelys_datalog_prepared_session_solve(
                                          session, permuted, 4u, &result), "%d");
                    TEST_ASSERT_TRUE(results_byte_identical(oracle_result, result));
                    TEST_ASSERT_TRUE(explanations_byte_identical(
                        &oracle_ruleset, oracle_result, result));
                    maelys_datalog_solve_result_free(result);
                    permutation_count++;
                }
            }
        }
    }
    TEST_ASSERT_EQUAL((size_t)24u, permutation_count, "%zu");

    maelys_datalog_input_fact_t different[2] = {
        binary_fact("member", "charlie", "other-team"),
        unary_fact("admin", "other-team"),
    };
    maelys_datalog_solve_result_t *middle_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, different, 2u, &middle_result), "%d");
    maelys_datalog_solve_result_free(middle_result);

    maelys_datalog_solve_result_t *final_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, canonical, 4u, &final_result), "%d");
    TEST_ASSERT_TRUE(results_byte_identical(oracle_result, final_result));
    TEST_ASSERT_TRUE(explanations_byte_identical(
        &oracle_ruleset, oracle_result, final_result));

    maelys_datalog_solve_result_free(final_result);
    maelys_datalog_solve_result_free(oracle_result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_filter_fresh_and_prepared_statistics_match(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t source;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&source), "%d");
    memset(source.rules, 0, sizeof(source.rules));
    source.rule_count = 0u;
    const char filter_source[] =
        "allow(U) :- starts_with(U, \"al\"), member(U, G).";
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_parse_ruleset(
                          &source, filter_source, strlen(filter_source)), "%d");

    maelys_datalog_input_fact_t facts[4];
    authorization_facts(facts, 0);
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&source, &session), "%d");
    maelys_datalog_solve_result_t *prepared = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 4u, &prepared), "%d");

    maelys_datalog_ruleset_t fresh_ruleset;
    maelys_datalog_fact_t *pool = calloc(
        MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*pool));
    TEST_ASSERT_NOT_NULL(pool);
    maelys_datalog_solve_result_t *fresh = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(
                          &source, facts, 4u, &fresh_ruleset, pool, &fresh), "%d");
    maelys_datalog_filter_statistics_t prepared_stats;
    maelys_datalog_filter_statistics_t fresh_stats;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_filter_statistics(
                          prepared, &prepared_stats), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_filter_statistics(
                          fresh, &fresh_stats), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(&prepared_stats,
                             &fresh_stats,
                             sizeof(prepared_stats)), "%d");
    TEST_ASSERT_TRUE(results_byte_identical(fresh, prepared));
    TEST_ASSERT_TRUE(explanations_byte_identical(
        &fresh_ruleset, fresh, prepared));

    maelys_datalog_predicate_id_t allow_id = 0u;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(
        &fresh_ruleset.registry, "allow", 1u, &allow_id));
    maelys_datalog_symbol_id_t bob_id = MAELYS_DATALOG_SYMBOL_ID_INVALID;
    int bob_found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_lookup_readonly(
                          &fresh_ruleset.symbols,
                          "bob",
                          strlen("bob"),
                          &bob_id,
                          &bob_found), "%d");
    TEST_ASSERT_TRUE(bob_found);
    maelys_datalog_fact_t absent;
    memset(&absent, 0, sizeof(absent));
    absent.predicate_id = allow_id;
    absent.arity = 1u;
    absent.terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
    absent.terms[0].as.symbol = bob_id;
    maelys_datalog_why_false_limits_t limits = {
        MAELYS_DATALOG_MAX_RULES,
        MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE,
        MAELYS_DATALOG_MAX_PROOF_DEPTH,
        MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS,
    };
    maelys_datalog_why_false_explanation_t *fresh_why =
        calloc(1u, sizeof(*fresh_why));
    maelys_datalog_why_false_explanation_t *prepared_why =
        calloc(1u, sizeof(*prepared_why));
    TEST_ASSERT_NOT_NULL(fresh_why);
    TEST_ASSERT_NOT_NULL(prepared_why);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fresh, &absent, &limits, fresh_why), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          prepared, &absent, &limits, prepared_why), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(fresh_why,
                             prepared_why,
                             sizeof(*fresh_why)), "%d");
    free(fresh_why);
    free(prepared_why);

    maelys_datalog_solve_result_free(fresh);
    maelys_datalog_solve_result_free(prepared);
    free(pool);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    maelys_datalog_ruleset_clear(&source);
    TEST_END();
}

static int assert_rendered_symbol(
    const maelys_datalog_solve_result_t *result,
    maelys_datalog_symbol_id_t id,
    const char *expected) {
    const char *text = NULL;
    size_t length = 0u;
    if (maelys_datalog_solve_result_symbol_text(
            result, id, &text, &length) != MAELYS_OK) {
        return 0;
    }
    return text && length == strlen(expected) &&
        memcmp(text, expected, length + 1u) == 0;
}

static int test_result_symbol_text_fresh_and_prepared(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t source;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_symbol_ruleset(&source), "%d");
    maelys_datalog_input_fact_t facts[1] = {
        binary_fact("member", "alice", "policy-team"),
    };

    maelys_datalog_prepared_session_t *session = NULL;
    maelys_datalog_solve_result_t *prepared = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&source, &session), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 1u, &prepared), "%d");
    maelys_datalog_symbol_id_t prepared_alice = 0u;
    maelys_datalog_symbol_id_t prepared_team = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alice", &prepared_alice, &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "policy-team", &prepared_team, &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(assert_rendered_symbol(prepared, prepared_alice, "alice"));
    TEST_ASSERT_TRUE(assert_rendered_symbol(
        prepared, prepared_team, "policy-team"));

    maelys_datalog_ruleset_t fresh_ruleset;
    maelys_datalog_fact_t fresh_pool[MAELYS_DATALOG_MAX_EDB_FACTS];
    maelys_datalog_solve_result_t *fresh = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(
                          &source, facts, 1u, &fresh_ruleset, fresh_pool, &fresh), "%d");
    maelys_datalog_symbol_id_t fresh_alice = 0u;
    maelys_datalog_symbol_id_t fresh_team = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_lookup_readonly(
                          &fresh_ruleset.symbols,
                          "alice",
                          strlen("alice"),
                          &fresh_alice,
                          &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_lookup_readonly(
                          &fresh_ruleset.symbols,
                          "policy-team",
                          strlen("policy-team"),
                          &fresh_team,
                          &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(assert_rendered_symbol(fresh, fresh_alice, "alice"));
    TEST_ASSERT_TRUE(assert_rendered_symbol(fresh, fresh_team, "policy-team"));

    maelys_datalog_solve_result_free(fresh);
    maelys_datalog_solve_result_free(prepared);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_result_symbol_text_errors_leave_outputs_untouched(void) {
    TEST_BEGIN();
    const char *text = (const char *)(uintptr_t)1u;
    size_t length = 991u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_symbol_text(
                          NULL, 1u, &text, &length), "%d");
    TEST_ASSERT_TRUE(text == (const char *)(uintptr_t)1u);
    TEST_ASSERT_EQUAL((size_t)991u, length, "%zu");

    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, NULL, 0u, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_symbol_text(
                          result, 1u, NULL, &length), "%d");
    TEST_ASSERT_EQUAL((size_t)991u, length, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_symbol_text(
                          result, 1u, &text, NULL), "%d");
    TEST_ASSERT_TRUE(text == (const char *)(uintptr_t)1u);
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_solve_result_symbol_text(
                          result,
                          MAELYS_DATALOG_SYMBOL_ID_INVALID,
                          &text,
                          &length), "%d");
    TEST_ASSERT_TRUE(text == (const char *)(uintptr_t)1u);
    TEST_ASSERT_EQUAL((size_t)991u, length, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_solve_result_symbol_text(
                          result, UINT16_MAX, &text, &length), "%d");
    TEST_ASSERT_TRUE(text == (const char *)(uintptr_t)1u);
    TEST_ASSERT_EQUAL((size_t)991u, length, "%zu");

    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_result_symbol_text_transaction_interpretation(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");

    maelys_datalog_input_fact_t a[2] = {
        binary_fact("member", "alpha", "team"),
        unary_fact("admin", "team"),
    };
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_symbol_id_t a_id = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(session, a, 2u, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alpha", &a_id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(assert_rendered_symbol(result, a_id, "alpha"));
    maelys_datalog_solve_result_free(result);

    maelys_datalog_input_fact_t b[2] = {
        binary_fact("member", "bravo", "team"),
        unary_fact("admin", "team"),
    };
    maelys_datalog_symbol_id_t b_id = 0u;
    result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(session, b, 2u, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "bravo", &b_id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(a_id, b_id, "%u");
    TEST_ASSERT_TRUE(assert_rendered_symbol(result, a_id, "bravo"));
    maelys_datalog_solve_result_free(result);

    maelys_datalog_symbol_id_t final_a_id = 0u;
    result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(session, a, 2u, &result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alpha", &final_a_id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(a_id, final_a_id, "%u");
    TEST_ASSERT_TRUE(assert_rendered_symbol(result, final_a_id, "alpha"));
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

static int test_result_symbol_text_read_only(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset), "%d");
    maelys_datalog_prepared_session_t *session = NULL;
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_input_fact_t facts[2] = {
        binary_fact("member", "alice", "team"),
        unary_fact("admin", "team"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(&ruleset, &session), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          session, facts, 2u, &result), "%d");
    maelys_datalog_symbol_id_t id = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_lookup_symbol(
                          session, "alice", &id, &found), "%d");
    TEST_ASSERT_TRUE(found);
    maelys_datalog_symbol_table_t before = session->working.symbols;
    const char *first = NULL;
    const char *second = NULL;
    size_t first_length = 0u;
    size_t second_length = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_symbol_text(
                          result, id, &first, &first_length), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_symbol_text(
                          result, id, &second, &second_length), "%d");
    TEST_ASSERT_TRUE(first == second);
    TEST_ASSERT_EQUAL(first_length, second_length, "%zu");
    TEST_ASSERT_EQUAL(0,
                      memcmp(&before,
                             &session->working.symbols,
                             sizeof(before)), "%d");
    maelys_datalog_solve_result_free(result);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_destroy(session), "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"prepared_session/order_independent_results_and_why_true",
         TEST_MODE_NON_BLOCKING, test_order_independent_results_and_why_true},
        {"prepared_session/matches_fresh_full_solve_oracle",
         TEST_MODE_NON_BLOCKING, test_matches_fresh_full_solve_oracle},
        {"prepared_session/reset_discards_prior_runtime_symbols",
         TEST_MODE_NON_BLOCKING, test_reset_discards_prior_runtime_symbols},
        {"prepared_session/fingerprint_snapshot_and_result_lease",
         TEST_MODE_NON_BLOCKING, test_fingerprint_snapshot_and_result_lease},
        {"prepared_session/argument_refusals_clear_outputs_and_preserve_session",
         TEST_MODE_NON_BLOCKING,
         test_argument_refusals_clear_outputs_and_preserve_session},
        {"prepared_session/refused_input_leaves_session_retryable",
         TEST_MODE_NON_BLOCKING, test_refused_input_leaves_session_retryable},
        {"prepared_session/typed_terms_are_materialized_exactly",
         TEST_MODE_NON_BLOCKING, test_typed_terms_are_materialized_exactly},
        {"prepared_session/input_text_is_borrowed_only_during_solve",
         TEST_MODE_NON_BLOCKING, test_input_text_is_borrowed_only_during_solve},
        {"prepared_session/all_permutations_and_aba_preserve_oracle",
         TEST_MODE_NON_BLOCKING, test_all_permutations_and_aba_preserve_oracle},
        {"prepared_session/filter_fresh_and_prepared_statistics_match",
         TEST_MODE_NON_BLOCKING, test_filter_fresh_and_prepared_statistics_match},
        {"prepared_session/result_symbol_text_fresh_and_prepared",
         TEST_MODE_NON_BLOCKING, test_result_symbol_text_fresh_and_prepared},
        {"prepared_session/result_symbol_text_errors_leave_outputs_untouched",
         TEST_MODE_NON_BLOCKING,
         test_result_symbol_text_errors_leave_outputs_untouched},
        {"prepared_session/result_symbol_text_transaction_interpretation",
         TEST_MODE_NON_BLOCKING,
         test_result_symbol_text_transaction_interpretation},
        {"prepared_session/result_symbol_text_read_only",
         TEST_MODE_NON_BLOCKING, test_result_symbol_text_read_only},
    };
    return test_main("maelys_datalog_prepared_session",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
