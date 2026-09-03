#include "include/maelys_datalog.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
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
static const char k_fingerprint[] =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";

typedef struct {
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_prepared_session_t *session;
    maelys_datalog_solve_result_t *result;
} why_false_fixture_t;

static maelys_datalog_why_false_limits_t generous_limits(void) {
    maelys_datalog_why_false_limits_t limits = {
        .max_candidate_rules = MAELYS_DATALOG_MAX_RULES,
        .max_substitutions_per_rule =
            MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE,
        .max_depth = MAELYS_DATALOG_MAX_PROOF_DEPTH,
        .max_diagnostics = MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS,
    };
    return limits;
}

static maelys_result_t make_ruleset(maelys_datalog_ruleset_t *ruleset,
                                    const char *source) {
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = maelys_datalog_ruleset_init(
        ruleset, "why.false", "authorization", k_fingerprint, 1);
    if (rc != MAELYS_OK) return rc;
    static const maelys_datalog_predicate_def_t defs[] = {
        {"member", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"admin", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"blocked", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"score", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"missing_a", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"missing_b", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"observed", 1u,
         MAELYS_DATALOG_PRED_KIND_EDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"trusted", 1u,
         MAELYS_DATALOG_PRED_KIND_POLICY_FACT |
             MAELYS_DATALOG_PRED_KIND_QUERY},
        {"helper", 1u,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"allow", 1u,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"deny", 1u,
         MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0u; i < sizeof(defs) / sizeof(defs[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_domain(
            &ruleset->registry,
            defs[i].name,
            defs[i].arity,
            defs[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    static const char *const atoms[] = {"alice", "alpha", "team", "zeta"};
    for (size_t i = 0u; i < sizeof(atoms) / sizeof(atoms[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_atom(
            &ruleset->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    ruleset->negation_supported = 1;
    ruleset->positive_recursion_supported = 1;
    return maelys_datalog_parse_ruleset(ruleset, source, strlen(source));
}

static maelys_datalog_input_term_t symbol_term(const char *value) {
    maelys_datalog_input_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_SYMBOL;
    term.as.symbol = value;
    return term;
}

static maelys_datalog_input_term_t integer_term(long long value) {
    maelys_datalog_input_term_t term;
    memset(&term, 0, sizeof(term));
    term.kind = MAELYS_DATALOG_TERM_INT;
    term.as.integer = value;
    return term;
}

static maelys_datalog_input_fact_t unary_fact(const char *predicate,
                                              const char *value) {
    maelys_datalog_input_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = predicate;
    fact.arity = 1u;
    fact.terms[0] = symbol_term(value);
    return fact;
}

static maelys_datalog_input_fact_t binary_symbols(const char *predicate,
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

static maelys_datalog_input_fact_t symbol_integer(const char *predicate,
                                                   const char *symbol,
                                                   long long integer) {
    maelys_datalog_input_fact_t fact;
    memset(&fact, 0, sizeof(fact));
    fact.predicate = predicate;
    fact.arity = 2u;
    fact.terms[0] = symbol_term(symbol);
    fact.terms[1] = integer_term(integer);
    return fact;
}

static maelys_result_t fixture_solve(
    why_false_fixture_t *fixture,
    const char *source,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count) {
    memset(fixture, 0, sizeof(*fixture));
    maelys_result_t rc = make_ruleset(&fixture->ruleset, source);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_prepared_session_create(
        &fixture->ruleset, &fixture->session);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_prepared_session_solve(
        fixture->session, facts, fact_count, &fixture->result);
}

static maelys_result_t solve_fresh_canonical(
    const maelys_datalog_ruleset_t *source,
    const maelys_datalog_input_fact_t *facts,
    size_t fact_count,
    maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_fact_t fact_pool[MAELYS_DATALOG_MAX_EDB_FACTS],
    maelys_datalog_solve_result_t **out_result) {
    *ruleset = *source;
    static const char *const symbols[] = {"alice", "team"};
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
        for (size_t term = 0u; term < facts[i].arity; term++) {
            terms[term].kind = facts[i].terms[term].kind;
            if (terms[term].kind == MAELYS_DATALOG_TERM_SYMBOL) {
                int found = 0;
                rc = maelys_datalog_symbol_lookup_readonly(
                    &ruleset->symbols,
                    facts[i].terms[term].as.symbol,
                    strlen(facts[i].terms[term].as.symbol),
                    &terms[term].as.symbol,
                    &found);
                if (rc != MAELYS_OK || !found) {
                    return rc != MAELYS_OK ? rc : MAELYS_ERR_INTERNAL;
                }
            } else if (terms[term].kind == MAELYS_DATALOG_TERM_INT) {
                terms[term].as.integer = facts[i].terms[term].as.integer;
            } else if (terms[term].kind == MAELYS_DATALOG_TERM_BOOL) {
                terms[term].as.boolean = facts[i].terms[term].as.boolean;
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

static int ruleset_query(const maelys_datalog_ruleset_t *ruleset,
                         const char *predicate,
                         const char *symbol,
                         maelys_datalog_fact_t *out_fact) {
    memset(out_fact, 0, sizeof(*out_fact));
    if (!maelys_datalog_predicate_registry_find(
            &ruleset->registry,
            predicate,
            1u,
            &out_fact->predicate_id)) {
        return 0;
    }
    out_fact->arity = 1u;
    out_fact->terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
    int found = 0;
    return maelys_datalog_symbol_lookup_readonly(
               &ruleset->symbols,
               symbol,
               strlen(symbol),
               &out_fact->terms[0].as.symbol,
               &found) == MAELYS_OK && found;
}

static void fixture_clear(why_false_fixture_t *fixture) {
    if (fixture->result) {
        maelys_datalog_solve_result_free(fixture->result);
        fixture->result = NULL;
    }
    if (fixture->session) {
        (void)maelys_datalog_prepared_session_destroy(fixture->session);
        fixture->session = NULL;
    }
}

static int fixture_query(why_false_fixture_t *fixture,
                         const char *predicate,
                         const char *symbol,
                         maelys_datalog_fact_t *out_fact) {
    memset(out_fact, 0, sizeof(*out_fact));
    if (!maelys_datalog_predicate_registry_find(
            &fixture->ruleset.registry,
            predicate,
            1u,
            &out_fact->predicate_id)) {
        return 0;
    }
    out_fact->arity = 1u;
    out_fact->terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
    int found = 0;
    if (maelys_datalog_prepared_session_lookup_symbol(
            fixture->session,
            symbol,
            &out_fact->terms[0].as.symbol,
            &found) != MAELYS_OK) {
        return 0;
    }
    return found;
}

static maelys_datalog_why_false_explanation_t *new_explanation(void) {
    return calloc(1u, sizeof(maelys_datalog_why_false_explanation_t));
}

typedef struct {
    const maelys_datalog_ruleset_t *ruleset;
    const maelys_datalog_prepared_session_t *session;
} normalized_vocabulary_t;

static const char *normalized_symbol_text(
    const normalized_vocabulary_t *vocabulary,
    maelys_datalog_symbol_id_t id) {
    const char *text = maelys_datalog_symbol_text(
        &vocabulary->ruleset->symbols, id);
    if (text || !vocabulary->session) return text;
    static const char *const known_symbols[] = {
        "alice", "alpha", "bob", "seed", "team", "zeta",
    };
    for (size_t index = 0u;
         index < sizeof(known_symbols) / sizeof(known_symbols[0]);
         index++) {
        maelys_datalog_symbol_id_t candidate =
            MAELYS_DATALOG_SYMBOL_ID_INVALID;
        int found = 0;
        if (maelys_datalog_prepared_session_lookup_symbol(
                vocabulary->session,
                known_symbols[index],
                &candidate,
                &found) == MAELYS_OK &&
            found && candidate == id) {
            return known_symbols[index];
        }
    }
    return NULL;
}

static int normalized_term_equal(
    const normalized_vocabulary_t *left_vocabulary,
    const maelys_datalog_term_t *left,
    const normalized_vocabulary_t *right_vocabulary,
    const maelys_datalog_term_t *right) {
    if (left->kind != right->kind) return 0;
    switch (left->kind) {
        case MAELYS_DATALOG_TERM_SYMBOL: {
            const char *left_text = normalized_symbol_text(
                left_vocabulary, left->as.symbol);
            const char *right_text = normalized_symbol_text(
                right_vocabulary, right->as.symbol);
            return left_text && right_text && strcmp(left_text, right_text) == 0;
        }
        case MAELYS_DATALOG_TERM_INT:
            return left->as.integer == right->as.integer;
        case MAELYS_DATALOG_TERM_BOOL:
            return left->as.boolean == right->as.boolean;
        case MAELYS_DATALOG_TERM_VAR:
            return left->as.variable == right->as.variable;
        default:
            return left->kind == 0;
    }
}

static int normalized_fact_equal(
    const normalized_vocabulary_t *left_vocabulary,
    const maelys_datalog_fact_t *left,
    const normalized_vocabulary_t *right_vocabulary,
    const maelys_datalog_fact_t *right) {
    if (left->predicate_id != right->predicate_id ||
        left->arity != right->arity) {
        return 0;
    }
    for (size_t term = 0u; term < left->arity; term++) {
        if (!normalized_term_equal(left_vocabulary,
                                   &left->terms[term],
                                   right_vocabulary,
                                   &right->terms[term])) {
            return 0;
        }
    }
    return 1;
}

static int normalized_pattern_equal(
    const normalized_vocabulary_t *left_vocabulary,
    const maelys_datalog_why_false_pattern_t *left,
    const normalized_vocabulary_t *right_vocabulary,
    const maelys_datalog_why_false_pattern_t *right) {
    if (left->predicate_id != right->predicate_id ||
        left->arity != right->arity ||
        left->unbound_term_mask != right->unbound_term_mask) {
        return 0;
    }
    for (size_t term = 0u; term < left->arity; term++) {
        if (!normalized_term_equal(left_vocabulary,
                                   &left->terms[term],
                                   right_vocabulary,
                                   &right->terms[term])) {
            return 0;
        }
    }
    return 1;
}

static int normalized_explanation_equal(
    const normalized_vocabulary_t *left_vocabulary,
    const maelys_datalog_why_false_explanation_t *left,
    const normalized_vocabulary_t *right_vocabulary,
    const maelys_datalog_why_false_explanation_t *right) {
    if (!normalized_fact_equal(
            left_vocabulary,
            &left->query,
            right_vocabulary,
            &right->query) ||
        left->status != right->status || left->summary != right->summary ||
        left->query_origin != right->query_origin ||
        left->limit_hits != right->limit_hits ||
        left->candidate_rule_count != right->candidate_rule_count ||
        left->substitution_count != right->substitution_count ||
        left->diagnostic_count != right->diagnostic_count) {
        return 0;
    }
    for (size_t index = 0u; index < left->diagnostic_count; index++) {
        const maelys_datalog_why_false_diagnostic_t *left_diagnostic =
            &left->diagnostics[index];
        const maelys_datalog_why_false_diagnostic_t *right_diagnostic =
            &right->diagnostics[index];
        if (left_diagnostic->rule_id != right_diagnostic->rule_id ||
            !normalized_fact_equal(left_vocabulary,
                                   &left_diagnostic->target_fact,
                                   right_vocabulary,
                                   &right_diagnostic->target_fact) ||
            left_diagnostic->bound_variable_mask !=
                right_diagnostic->bound_variable_mask ||
            left_diagnostic->support_count != right_diagnostic->support_count ||
            left_diagnostic->depth != right_diagnostic->depth) {
            return 0;
        }
        for (size_t variable = 0u;
             variable < MAELYS_DATALOG_MAX_RULE_VARIABLES;
             variable++) {
            if ((left_diagnostic->bound_variable_mask &
                 ((uint32_t)1u << variable)) == 0u) {
                continue;
            }
            if (!normalized_term_equal(
                    left_vocabulary,
                    &left_diagnostic->substitution[variable],
                    right_vocabulary,
                    &right_diagnostic->substitution[variable])) {
                return 0;
            }
        }
        for (size_t support = 0u;
             support < left_diagnostic->support_count;
             support++) {
            const maelys_datalog_why_false_support_t *left_support =
                &left_diagnostic->supports[support];
            const maelys_datalog_why_false_support_t *right_support =
                &right_diagnostic->supports[support];
            if (left_support->body_index != right_support->body_index ||
                left_support->origin != right_support->origin ||
                !normalized_fact_equal(left_vocabulary,
                                       &left_support->fact,
                                       right_vocabulary,
                                       &right_support->fact)) {
                return 0;
            }
        }
        const maelys_datalog_why_false_obstacle_t *left_obstacle =
            &left_diagnostic->obstacle;
        const maelys_datalog_why_false_obstacle_t *right_obstacle =
            &right_diagnostic->obstacle;
        if (left_obstacle->kind != right_obstacle->kind ||
            left_obstacle->origin != right_obstacle->origin ||
            left_obstacle->body_index != right_obstacle->body_index ||
            left_obstacle->op != right_obstacle->op ||
            !normalized_pattern_equal(left_vocabulary,
                                      &left_obstacle->pattern,
                                      right_vocabulary,
                                      &right_obstacle->pattern) ||
            !normalized_term_equal(left_vocabulary,
                                   &left_obstacle->lhs,
                                   right_vocabulary,
                                   &right_obstacle->lhs) ||
            !normalized_term_equal(left_vocabulary,
                                   &left_obstacle->rhs,
                                   right_vocabulary,
                                   &right_obstacle->rhs)) {
            return 0;
        }
    }
    return 1;
}

static int test_negative_contradiction_with_positive_support(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- member(U, G), admin(G), not(blocked(U)).";
    maelys_datalog_input_fact_t facts[3] = {
        binary_symbols("member", "alice", "team"),
        unary_fact("admin", "team"),
        unary_fact("blocked", "alice"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 3u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    const maelys_datalog_why_false_diagnostic_t *diagnostic =
        &explanation->diagnostics[0];
    TEST_ASSERT_EQUAL(2u, diagnostic->support_count, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED,
                      diagnostic->obstacle.kind, "%u");
    TEST_ASSERT_EQUAL(2u, diagnostic->obstacle.body_index, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB,
                      diagnostic->obstacle.origin, "%u");
    TEST_ASSERT_EQUAL(0u,
                      diagnostic->obstacle.pattern.unbound_term_mask, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_positive_no_match_keeps_unbound_pattern(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source = "allow(U) :- member(U, G), admin(G).";
    maelys_datalog_input_fact_t seed = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &seed, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    const maelys_datalog_why_false_obstacle_t *obstacle =
        &explanation->diagnostics[0].obstacle;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_POSITIVE_NO_MATCH,
                      obstacle->kind, "%u");
    TEST_ASSERT_EQUAL(0u, obstacle->body_index, "%u");
    TEST_ASSERT_EQUAL((uint8_t)(1u << 1u),
                      obstacle->pattern.unbound_term_mask, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_TERM_VAR,
                      obstacle->pattern.terms[1].kind, "%d");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_comparison_false_uses_ground_operands(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source = "allow(U) :- score(U, S), S >= 10.";
    maelys_datalog_input_fact_t fact = symbol_integer("score", "alice", 5);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    const maelys_datalog_why_false_diagnostic_t *diagnostic =
        &explanation->diagnostics[0];
    TEST_ASSERT_EQUAL(1u, diagnostic->support_count, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_COMPARISON_FALSE,
                      diagnostic->obstacle.kind, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_CMP_GTE,
                      diagnostic->obstacle.op, "%u");
    TEST_ASSERT_EQUAL(5LL, diagnostic->obstacle.lhs.as.integer, "%lld");
    TEST_ASSERT_EQUAL(10LL, diagnostic->obstacle.rhs.as.integer, "%lld");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_positive_support_can_come_from_materialized_idb(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "helper(U) :- observed(U).\n"
        "allow(U) :- helper(U), not(blocked(U)).";
    const maelys_datalog_input_fact_t facts[2] = {
        unary_fact("observed", "alice"),
        unary_fact("blocked", "alice"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 2u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(1u, explanation->diagnostics[0].support_count, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                      explanation->diagnostics[0].supports[0].origin, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED,
                      explanation->diagnostics[0].obstacle.kind, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_present_and_no_candidate_states(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "trusted(\"alice\").\n"
        "allow(U) :- observed(U).";
    maelys_datalog_input_fact_t fact = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);

    maelys_datalog_fact_t present;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &present));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &present, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_NOT_APPLICABLE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_IDB,
                      explanation->query_origin, "%u");

    maelys_datalog_fact_t absent;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "deny", "alice", &absent));
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &absent, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_SUMMARY_NO_CANDIDATE_RULE,
                      explanation->summary, "%u");
    TEST_ASSERT_EQUAL((size_t)0u, explanation->diagnostic_count, "%zu");

    maelys_datalog_fact_t edb_present;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "observed", "alice", &edb_present));
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &edb_present, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB,
                      explanation->query_origin, "%u");

    maelys_datalog_fact_t policy_present;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "trusted", "alice", &policy_present));
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &policy_present, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_NOT_APPLICABLE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT,
                      explanation->query_origin, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_each_non_depth_limit_is_independently_observable(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- member(U, G), admin(G).\n"
        "allow(U) :- missing_a(U).\n"
        "allow(U) :- missing_b(U).";
    maelys_datalog_input_fact_t facts[2] = {
        binary_symbols("member", "alice", "a"),
        binary_symbols("member", "alice", "b"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 2u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);

    maelys_datalog_why_false_limits_t limits = generous_limits();
    limits.max_candidate_rules = 1u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      explanation->status, "%u");
    TEST_ASSERT_TRUE((explanation->limit_hits &
                      MAELYS_DATALOG_WHY_FALSE_LIMIT_CANDIDATE_RULES) != 0u);
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_CANDIDATE_RULES,
                      explanation->limit_hits, "%u");

    limits = generous_limits();
    limits.max_substitutions_per_rule = 1u;
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_SUBSTITUTIONS,
                      explanation->limit_hits, "%u");

    limits = generous_limits();
    limits.max_diagnostics = 1u;
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_TRUE((explanation->limit_hits &
                      MAELYS_DATALOG_WHY_FALSE_LIMIT_DIAGNOSTICS) != 0u);
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_DIAGNOSTICS,
                      explanation->limit_hits, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_public_order_does_not_follow_join_planner_score(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    /* Head binding makes lexical body 0 safe. The production planner scores
     * the later positive atom above the negation; Why-false must still select
     * the stable lexical-safe order and expose no planner-created support. */
    const char *source =
        "allow(U) :- not(blocked(U)), member(U, G).";
    maelys_datalog_input_fact_t facts[2] = {
        unary_fact("blocked", "alice"),
        binary_symbols("member", "alice", "team"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 2u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(0u, explanation->diagnostics[0].support_count, "%u");
    TEST_ASSERT_EQUAL(0u,
                      explanation->diagnostics[0].obstacle.body_index, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED,
                      explanation->diagnostics[0].obstacle.kind, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_executable_order_binds_before_earlier_obstacle(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    /* Body 0 is lexically first but unsafe until body 1 binds G. The public
     * obstacle must retain body 0's original lexical identity. */
    const char *source =
        "allow(U) :- not(blocked(G)), member(U, G).";
    maelys_datalog_input_fact_t facts[2] = {
        binary_symbols("member", "alice", "team"),
        unary_fact("blocked", "team"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 2u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(1u, explanation->diagnostics[0].support_count, "%u");
    TEST_ASSERT_EQUAL(1u,
                      explanation->diagnostics[0].supports[0].body_index,
                      "%u");
    TEST_ASSERT_EQUAL(0u,
                      explanation->diagnostics[0].obstacle.body_index,
                      "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_NEGATIVE_CONTRADICTED,
                      explanation->diagnostics[0].obstacle.kind, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_public_order_uses_symbol_text_not_symbol_id(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    /* Policy symbols are deliberately interned in zeta-before-alpha order. */
    const char *source =
        "trusted(\"zeta\").\n"
        "trusted(\"alpha\").\n"
        "allow(U) :- observed(U), trusted(G), admin(G).";
    maelys_datalog_input_fact_t fact = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL((size_t)2u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(2u, explanation->diagnostics[0].support_count, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_EDB,
                      explanation->diagnostics[0].supports[0].origin, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_ORIGIN_POLICY_FACT,
                      explanation->diagnostics[0].supports[1].origin, "%u");
    TEST_ASSERT_EQUAL_STRING(
        "alpha",
        maelys_datalog_symbol_text(
            &fixture.ruleset.symbols,
            explanation->diagnostics[0].substitution[6u].as.symbol));
    TEST_ASSERT_EQUAL_STRING(
        "zeta",
        maelys_datalog_symbol_text(
            &fixture.ruleset.symbols,
            explanation->diagnostics[1].substitution[6u].as.symbol));
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_truncated_prefix_is_stable_across_symbol_vocabularies(void) {
    TEST_BEGIN();
    const char *zeta_first =
        "trusted(\"zeta\").\n"
        "trusted(\"alpha\").\n"
        "allow(U) :- observed(U), trusted(G), admin(G).";
    const char *alpha_first =
        "trusted(\"alpha\").\n"
        "trusted(\"zeta\").\n"
        "allow(U) :- observed(U), trusted(G), admin(G).";
    const maelys_datalog_input_fact_t fact =
        unary_fact("observed", "alice");
    why_false_fixture_t first;
    why_false_fixture_t second;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&first, zeta_first, &fact, 1u), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&second, alpha_first, &fact, 1u), "%d");

    maelys_datalog_fact_t first_query;
    maelys_datalog_fact_t second_query;
    TEST_ASSERT_TRUE(fixture_query(&first, "allow", "alice", &first_query));
    TEST_ASSERT_TRUE(fixture_query(&second, "allow", "alice", &second_query));
    maelys_datalog_why_false_limits_t limits = generous_limits();
    /* observed(alice) consumes extension 1; only the first canonical trusted
     * value may consume extension 2 and reach the missing admin premise. */
    limits.max_substitutions_per_rule = 2u;
    maelys_datalog_why_false_explanation_t *first_explanation =
        new_explanation();
    maelys_datalog_why_false_explanation_t *second_explanation =
        new_explanation();
    TEST_ASSERT_NOT_NULL(first_explanation);
    TEST_ASSERT_NOT_NULL(second_explanation);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          first.result,
                          &first_query,
                          &limits,
                          first_explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          second.result,
                          &second_query,
                          &limits,
                          second_explanation), "%d");

    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      first_explanation->status, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      second_explanation->status, "%u");
    TEST_ASSERT_EQUAL(
        (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_SUBSTITUTIONS,
        first_explanation->limit_hits,
        "%u");
    TEST_ASSERT_EQUAL(first_explanation->limit_hits,
                      second_explanation->limit_hits, "%u");
    TEST_ASSERT_EQUAL((size_t)1u,
                      first_explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(first_explanation->diagnostic_count,
                      second_explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL_STRING(
        "alpha",
        maelys_datalog_symbol_text(
            &first.ruleset.symbols,
            first_explanation->diagnostics[0]
                .substitution[6u].as.symbol));
    TEST_ASSERT_EQUAL_STRING(
        "alpha",
        maelys_datalog_symbol_text(
            &second.ruleset.symbols,
            second_explanation->diagnostics[0]
                .substitution[6u].as.symbol));
    TEST_ASSERT_EQUAL(first_explanation->diagnostics[0].rule_id,
                      second_explanation->diagnostics[0].rule_id, "%zu");
    TEST_ASSERT_EQUAL(first_explanation->diagnostics[0].obstacle.kind,
                      second_explanation->diagnostics[0].obstacle.kind, "%u");
    TEST_ASSERT_EQUAL(first_explanation->diagnostics[0].obstacle.body_index,
                      second_explanation->diagnostics[0].obstacle.body_index,
                      "%u");

    free(second_explanation);
    free(first_explanation);
    fixture_clear(&second);
    fixture_clear(&first);
    TEST_END();
}


static int test_positive_cycle_and_depth_are_bounded(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- helper(U).\n"
        "helper(U) :- allow(U).";
    maelys_datalog_input_fact_t seed = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &seed, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);

    maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      explanation->status, "%u");
    int found_cycle = 0;
    for (size_t i = 0u; i < explanation->diagnostic_count; i++) {
        if (explanation->diagnostics[i].obstacle.kind ==
            MAELYS_DATALOG_WHY_FALSE_OBSTACLE_RECURSIVE_NO_BASE_SUPPORT) {
            found_cycle = 1;
        }
    }
    TEST_ASSERT_TRUE(found_cycle);

    limits.max_depth = 1u;
    memset(explanation, 0, sizeof(*explanation));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      explanation->status, "%u");
    TEST_ASSERT_TRUE((explanation->limit_hits &
                      MAELYS_DATALOG_WHY_FALSE_LIMIT_DEPTH) != 0u);
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_diagnostic_top_k_is_canonical(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- helper(U).\n"
        "helper(U) :- missing_a(U).";
    const maelys_datalog_input_fact_t seed = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &seed, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    maelys_datalog_why_false_limits_t limits = generous_limits();
    limits.max_diagnostics = 1u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_DIAGNOSTICS,
                      explanation->limit_hits, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    /* The global canonical frontier emits the outer rule (id 0) first. The
     * bounded diagnostic result must therefore retain that public minimum. */
    TEST_ASSERT_EQUAL(fixture.ruleset.rules[0].rule_id,
                      explanation->diagnostics[0].rule_id, "%zu");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_candidate_rule_bound_uses_available_canonical_frontier(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- helper(U).\n"
        "allow(U) :- missing_a(U).\n"
        "helper(U) :- missing_b(U).";
    const maelys_datalog_input_fact_t seed = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &seed, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    maelys_datalog_why_false_limits_t limits = generous_limits();
    limits.max_candidate_rules = 2u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL(
        (uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_CANDIDATE_RULES,
        explanation->limit_hits,
        "%u");
    TEST_ASSERT_EQUAL((size_t)2u, explanation->candidate_rule_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)2u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(fixture.ruleset.rules[0].rule_id,
                      explanation->diagnostics[0].rule_id,
                      "%zu");
    TEST_ASSERT_EQUAL(fixture.ruleset.rules[1].rule_id,
                      explanation->diagnostics[1].rule_id,
                      "%zu");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_repeated_and_permuted_inputs_are_identical(void) {
    TEST_BEGIN();
    const char *source =
        "allow(U) :- member(U, G), admin(G), not(blocked(U)).";
    const maelys_datalog_input_fact_t facts[3] = {
        binary_symbols("member", "alice", "team"),
        unary_fact("admin", "team"),
        unary_fact("blocked", "alice"),
    };
    static const uint8_t permutations[6][3] = {
        {0u, 1u, 2u}, {0u, 2u, 1u}, {1u, 0u, 2u},
        {1u, 2u, 0u}, {2u, 0u, 1u}, {2u, 1u, 0u},
    };
    maelys_datalog_input_fact_t ordered[3];
    for (size_t index = 0u; index < 3u; index++) {
        ordered[index] = facts[permutations[0][index]];
    }
    why_false_fixture_t first;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&first, source, ordered, 3u), "%d");
    maelys_datalog_fact_t first_query;
    TEST_ASSERT_TRUE(fixture_query(&first, "allow", "alice", &first_query));
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    maelys_datalog_why_false_explanation_t *first_explanation = new_explanation();
    maelys_datalog_why_false_explanation_t *repeat_explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(first_explanation);
    TEST_ASSERT_NOT_NULL(repeat_explanation);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          first.result, &first_query, &limits, first_explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          first.result, &first_query, &limits, repeat_explanation), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(first_explanation,
                             repeat_explanation,
                             sizeof(*first_explanation)), "%d");
    const normalized_vocabulary_t first_vocabulary = {
        &first.ruleset, first.session};

    for (size_t permutation = 1u; permutation < 6u; permutation++) {
        for (size_t index = 0u; index < 3u; index++) {
            ordered[index] = facts[permutations[permutation][index]];
        }
        why_false_fixture_t current;
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          fixture_solve(&current, source, ordered, 3u), "%d");
        maelys_datalog_fact_t current_query;
        TEST_ASSERT_TRUE(fixture_query(
            &current, "allow", "alice", &current_query));
        maelys_datalog_why_false_explanation_t *current_explanation =
            new_explanation();
        TEST_ASSERT_NOT_NULL(current_explanation);
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_explain_absent_solved_fact(
                              current.result,
                              &current_query,
                              &limits,
                              current_explanation), "%d");
        const normalized_vocabulary_t current_vocabulary = {
            &current.ruleset, current.session};
        TEST_ASSERT_TRUE(normalized_explanation_equal(
            &first_vocabulary,
            first_explanation,
            &current_vocabulary,
            current_explanation));
        free(current_explanation);
        fixture_clear(&current);
    }
    free(first_explanation);
    free(repeat_explanation);
    fixture_clear(&first);
    TEST_END();
}

static int test_fresh_and_prepared_explanations_are_identical(void) {
    TEST_BEGIN();
    why_false_fixture_t prepared;
    const char *source =
        "allow(U) :- member(U, G), admin(G), not(blocked(U)).";
    maelys_datalog_input_fact_t facts[3] = {
        binary_symbols("member", "alice", "team"),
        unary_fact("admin", "team"),
        unary_fact("blocked", "alice"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&prepared, source, facts, 3u), "%d");

    maelys_datalog_ruleset_t *fresh_ruleset =
        malloc(sizeof(*fresh_ruleset));
    maelys_datalog_fact_t *fresh_pool =
        calloc(MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*fresh_pool));
    maelys_datalog_solve_result_t *fresh_result = NULL;
    TEST_ASSERT_NOT_NULL(fresh_ruleset);
    TEST_ASSERT_NOT_NULL(fresh_pool);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_fresh_canonical(&prepared.ruleset,
                                            facts,
                                            3u,
                                            fresh_ruleset,
                                            fresh_pool,
                                            &fresh_result), "%d");

    maelys_datalog_fact_t prepared_query;
    maelys_datalog_fact_t fresh_query;
    TEST_ASSERT_TRUE(fixture_query(
        &prepared, "allow", "alice", &prepared_query));
    TEST_ASSERT_TRUE(ruleset_query(
        fresh_ruleset, "allow", "alice", &fresh_query));
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    maelys_datalog_why_false_explanation_t *prepared_explanation =
        new_explanation();
    maelys_datalog_why_false_explanation_t *fresh_explanation =
        new_explanation();
    TEST_ASSERT_NOT_NULL(prepared_explanation);
    TEST_ASSERT_NOT_NULL(fresh_explanation);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          prepared.result,
                          &prepared_query,
                          &limits,
                          prepared_explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fresh_result,
                          &fresh_query,
                          &limits,
                          fresh_explanation), "%d");
    const normalized_vocabulary_t prepared_vocabulary = {
        &prepared.ruleset, prepared.session};
    const normalized_vocabulary_t fresh_vocabulary = {
        fresh_ruleset, NULL};
    TEST_ASSERT_TRUE(normalized_explanation_equal(
        &prepared_vocabulary,
        prepared_explanation,
        &fresh_vocabulary,
        fresh_explanation));

    free(fresh_explanation);
    free(prepared_explanation);
    maelys_datalog_solve_result_free(fresh_result);
    free(fresh_pool);
    free(fresh_ruleset);
    fixture_clear(&prepared);
    TEST_END();
}

static int test_extraction_is_read_only_and_preserves_why_true(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "allow(U) :- member(U, G), admin(G), not(blocked(U)).";
    maelys_datalog_input_fact_t facts[5] = {
        binary_symbols("member", "alice", "team"),
        binary_symbols("member", "bob", "team"),
        unary_fact("admin", "team"),
        unary_fact("blocked", "alice"),
        unary_fact("observed", "seed"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 5u), "%d");

    maelys_datalog_fact_t absent;
    maelys_datalog_fact_t present;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &absent));
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "bob", &present));

    maelys_datalog_proof_tree_t *proof_before = malloc(sizeof(*proof_before));
    maelys_datalog_explanation_t *why_true_before =
        calloc(1u, sizeof(*why_true_before));
    maelys_datalog_explanation_t *why_true_after =
        calloc(1u, sizeof(*why_true_after));
    maelys_datalog_why_false_explanation_t *why_false = new_explanation();
    TEST_ASSERT_NOT_NULL(proof_before);
    TEST_ASSERT_NOT_NULL(why_true_before);
    TEST_ASSERT_NOT_NULL(why_true_after);
    TEST_ASSERT_NOT_NULL(why_false);
    const maelys_datalog_proof_tree_t *proof =
        maelys_datalog_solve_result_proof(fixture.result);
    TEST_ASSERT_NOT_NULL(proof);
    memcpy(proof_before, proof, sizeof(*proof_before));

    const maelys_datalog_fact_t *idb_before = NULL;
    const uint16_t *indices_before = NULL;
    size_t idb_count = 0u;
    size_t index_count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(
                          fixture.result, &idb_before, &idb_count), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_proof_indices(
                          fixture.result, &indices_before, &index_count), "%d");
    maelys_datalog_fact_t *idb_snapshot =
        malloc(idb_count * sizeof(*idb_snapshot));
    uint16_t *index_snapshot =
        malloc(index_count * sizeof(*index_snapshot));
    TEST_ASSERT_NOT_NULL(idb_snapshot);
    TEST_ASSERT_NOT_NULL(index_snapshot);
    memcpy(idb_snapshot, idb_before, idb_count * sizeof(*idb_snapshot));
    memcpy(index_snapshot,
           indices_before,
           index_count * sizeof(*index_snapshot));

    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(
                          fixture.result, &present, why_true_before), "%d");
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &absent, &limits, why_false), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(
                          fixture.result, &present, why_true_after), "%d");

    TEST_ASSERT_EQUAL(0,
                      memcmp(why_true_before,
                             why_true_after,
                             sizeof(*why_true_before)), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(proof_before, proof, sizeof(*proof_before)), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(idb_snapshot,
                             idb_before,
                             idb_count * sizeof(*idb_snapshot)), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(index_snapshot,
                             indices_before,
                             index_count * sizeof(*index_snapshot)), "%d");

    free(index_snapshot);
    free(idb_snapshot);
    free(why_false);
    free(why_true_after);
    free(why_true_before);
    free(proof_before);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_invalid_limits_leave_output_untouched(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source = "allow(U) :- observed(U).";
    maelys_datalog_input_fact_t fact = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    maelys_datalog_why_false_explanation_t *sentinel = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_NOT_NULL(sentinel);
    memset(explanation, 0x5Au, sizeof(*explanation));
    memcpy(sentinel, explanation, sizeof(*sentinel));
    maelys_datalog_why_false_limits_t limits = generous_limits();
    limits.max_depth = 0u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(explanation, sentinel, sizeof(*explanation)), "%d");
    free(explanation);
    free(sentinel);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_query_and_whitelist_guards_leave_output_untouched(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    const char *source =
        "allow(U) :- observed(U).\n"
        "deny(U) :- observed(U).";
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&fixture.ruleset, source), "%d");
    fixture.ruleset.enforces_query_whitelist = 1;
    fixture.ruleset.query_whitelist_count = 2u;
    (void)strcpy(fixture.ruleset.query_whitelist[0].name, "allow");
    fixture.ruleset.query_whitelist[0].arity = 1u;
    (void)strcpy(fixture.ruleset.query_whitelist[1].name, "admin");
    fixture.ruleset.query_whitelist[1].arity = 1u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_create(
                          &fixture.ruleset, &fixture.session), "%d");
    const maelys_datalog_input_fact_t fact = unary_fact("observed", "alice");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_prepared_session_solve(
                          fixture.session, &fact, 1u, &fixture.result), "%d");

    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    maelys_datalog_why_false_explanation_t *sentinel = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_NOT_NULL(sentinel);
    memset(explanation, 0xA5, sizeof(*explanation));
    memcpy(sentinel, explanation, sizeof(*sentinel));
    const maelys_datalog_why_false_limits_t limits = generous_limits();

    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "deny", "alice", &query));
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(explanation, sentinel, sizeof(*explanation)), "%d");

    TEST_ASSERT_TRUE(fixture_query(&fixture, "admin", "alice", &query));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(explanation, sentinel, sizeof(*explanation)), "%d");

    free(sentinel);
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_unresolvable_symbol_fails_closed_and_preserves_output(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const char *source =
        "helper(U) :- observed(U).\n"
        "allow(\"alice\") :- helper(G), admin(G).";
    const maelys_datalog_input_fact_t facts[2] = {
        unary_fact("observed", "alpha"),
        unary_fact("observed", "zeta"),
    };
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, facts, 2u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));

    const maelys_datalog_fact_t *idb_facts = NULL;
    size_t idb_count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_test_solve_result_idb_facts(
                          fixture.result, &idb_facts, &idb_count), "%d");
    TEST_ASSERT_TRUE(idb_count >= 2u);
    maelys_datalog_fact_t *mutable_idb = (maelys_datalog_fact_t *)idb_facts;
    const maelys_datalog_symbol_id_t saved = mutable_idb[0].terms[0].as.symbol;
    mutable_idb[0].terms[0].as.symbol = UINT32_MAX;

    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    maelys_datalog_why_false_explanation_t *sentinel = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_NOT_NULL(sentinel);
    memset(explanation, 0x3C, sizeof(*explanation));
    memcpy(sentinel, explanation, sizeof(*sentinel));
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(0,
                      memcmp(explanation, sentinel, sizeof(*explanation)), "%d");

    mutable_idb[0].terms[0].as.symbol = saved;
    free(sentinel);
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_filter_false_obstacle_and_cost_are_explicit(void) {
    TEST_BEGIN();
    why_false_fixture_t fixture;
    const maelys_datalog_input_fact_t facts[] = {
        binary_symbols("member", "alice", "team"),
    };
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        fixture_solve(&fixture,
                      "allow(U) :- member(U, G), starts_with(U, \"z\").",
                      facts,
                      sizeof(facts) / sizeof(facts[0])), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", "alice", &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_FILTER_FALSE,
                      explanation->diagnostics[0].obstacle.kind, "%u");
    TEST_ASSERT_EQUAL((uint16_t)1u,
                      explanation->diagnostics[0].obstacle.body_index, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, explanation->filter_cost_units, "%zu");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

static int test_filter_cost_last_admitted_then_truncated(void) {
    TEST_BEGIN();
    char pattern[MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES + 1u];
    char value[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    memset(pattern, 'b', sizeof(pattern) - 1u);
    pattern[sizeof(pattern) - 1u] = '\0';
    memset(value, 'a', sizeof(value) - 1u);
    value[sizeof(value) - 1u] = '\0';
    char clause[512];
    int written = snprintf(
        clause,
        sizeof(clause),
        "allow(U) :- contains(U, \"%s\"), missing_a(U).\n",
        pattern);
    TEST_ASSERT_TRUE(written > 0);
    char source[4096];
    source[0] = '\0';
    for (size_t i = 0u; i < 4u; i++) {
        TEST_ASSERT_TRUE(strlen(source) + strlen(clause) < sizeof(source));
        (void)strcat(source, clause);
    }
    maelys_datalog_input_fact_t fact = unary_fact("observed", value);
    why_false_fixture_t fixture;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    maelys_datalog_fact_t query;
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", value, &query));
    maelys_datalog_why_false_explanation_t *explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    const maelys_datalog_why_false_limits_t limits = generous_limits();
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS,
                      explanation->filter_cost_units, "%zu");
    TEST_ASSERT_EQUAL((uint8_t)0u, explanation->limit_hits, "%u");
    free(explanation);
    fixture_clear(&fixture);

    TEST_ASSERT_TRUE(strlen(source) + strlen(clause) < sizeof(source));
    (void)strcat(source, clause);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      fixture_solve(&fixture, source, &fact, 1u), "%d");
    TEST_ASSERT_TRUE(fixture_query(&fixture, "allow", value, &query));
    explanation = new_explanation();
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          fixture.result, &query, &limits, explanation), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_TRUNCATED,
                      explanation->status, "%u");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_WHY_FALSE_FILTER_COST_UNITS,
                      explanation->filter_cost_units, "%zu");
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_WHY_FALSE_LIMIT_FILTER_COST,
                      explanation->limit_hits, "%u");
    free(explanation);
    fixture_clear(&fixture);
    TEST_END();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const test_case_t cases[] = {
        {"why_false/semantic_negative_contradiction_with_positive_support",
         TEST_MODE_NON_BLOCKING,
         test_negative_contradiction_with_positive_support},
        {"why_false/semantic_positive_no_match_keeps_unbound_pattern",
         TEST_MODE_NON_BLOCKING,
         test_positive_no_match_keeps_unbound_pattern},
        {"why_false/semantic_comparison_false_uses_ground_operands",
         TEST_MODE_NON_BLOCKING,
         test_comparison_false_uses_ground_operands},
        {"why_false/semantic_materialized_idb_support",
         TEST_MODE_NON_BLOCKING,
         test_positive_support_can_come_from_materialized_idb},
        {"why_false/semantic_present_and_no_candidate_states",
         TEST_MODE_NON_BLOCKING,
         test_present_and_no_candidate_states},
        {"why_false/bounds_each_non_depth_limit_is_independently_observable",
         TEST_MODE_NON_BLOCKING,
         test_each_non_depth_limit_is_independently_observable},
        {"why_false/executable_order_does_not_follow_join_planner_score",
         TEST_MODE_NON_BLOCKING,
         test_public_order_does_not_follow_join_planner_score},
        {"why_false/executable_order_binds_before_earlier_obstacle",
         TEST_MODE_NON_BLOCKING,
         test_executable_order_binds_before_earlier_obstacle},
        {"why_false/determinism_public_order_uses_symbol_text_not_symbol_id",
         TEST_MODE_NON_BLOCKING,
         test_public_order_uses_symbol_text_not_symbol_id},
        {"why_false/determinism_truncated_prefix_across_vocabularies",
         TEST_MODE_NON_BLOCKING,
         test_truncated_prefix_is_stable_across_symbol_vocabularies},
        {"why_false/bounds_positive_cycle_and_depth",
         TEST_MODE_NON_BLOCKING,
         test_positive_cycle_and_depth_are_bounded},
        {"why_false/bounds_diagnostic_top_k_is_canonical",
         TEST_MODE_NON_BLOCKING,
         test_diagnostic_top_k_is_canonical},
        {"why_false/bounds_candidate_rule_global_canonical_frontier",
         TEST_MODE_NON_BLOCKING,
         test_candidate_rule_bound_uses_available_canonical_frontier},
        {"why_false/determinism_repeated_and_permuted_inputs",
         TEST_MODE_NON_BLOCKING,
         test_repeated_and_permuted_inputs_are_identical},
        {"why_false/determinism_fresh_and_prepared_explanations",
         TEST_MODE_NON_BLOCKING,
         test_fresh_and_prepared_explanations_are_identical},
        {"why_false/non_mutation_extraction_preserves_why_true",
         TEST_MODE_NON_BLOCKING,
         test_extraction_is_read_only_and_preserves_why_true},
        {"why_false/non_mutation_invalid_limits_leave_output_untouched",
         TEST_MODE_NON_BLOCKING,
         test_invalid_limits_leave_output_untouched},
        {"why_false/semantic_query_and_whitelist_guards",
         TEST_MODE_NON_BLOCKING,
         test_query_and_whitelist_guards_leave_output_untouched},
        {"why_false/non_mutation_unresolvable_symbol_fails_closed",
         TEST_MODE_NON_BLOCKING,
         test_unresolvable_symbol_fails_closed_and_preserves_output},
        {"why_false/filter_false_obstacle_and_cost_are_explicit",
         TEST_MODE_NON_BLOCKING,
         test_filter_false_obstacle_and_cost_are_explicit},
        {"why_false/filter_cost_last_admitted_then_truncated",
         TEST_MODE_NON_BLOCKING,
         test_filter_cost_last_admitted_then_truncated},
    };
    return test_main("maelys_datalog_why_false",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
