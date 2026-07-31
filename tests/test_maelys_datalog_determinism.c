#include "src/core/maelys_datalog_edb.h"
#include "src/core/maelys_datalog_explanation_format.h"
#include "src/core/maelys_datalog_parser.h"
#include "src/core/maelys_datalog_predicate_registry.h"
#include "src/core/maelys_datalog_solver.h"
#include "src/core/maelys_datalog_symbol_table.h"
#include "tests/helpers/test_framework.h"

#include <stdio.h>
#include <string.h>

static const char k_zero_sha[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static maelys_result_t add_predicate(maelys_datalog_ruleset_t *ruleset,
                                     const char *name,
                                     size_t arity,
                                     unsigned kind_flags) {
    return maelys_datalog_predicate_registry_add_domain(&ruleset->registry,
                                                        name,
                                                        arity,
                                                        kind_flags);
}

static maelys_result_t init_ruleset_base(maelys_datalog_ruleset_t *ruleset,
                                         const char *policy_id) {
    memset(ruleset, 0, sizeof(*ruleset));
    return maelys_datalog_ruleset_init(ruleset,
                                       policy_id,
                                       "determinism",
                                       k_zero_sha,
                                       1);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset,
                                         const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static void add_symbol_pair(maelys_datalog_ruleset_t *ruleset,
                            maelys_datalog_edb_t *edb,
                            const char *predicate,
                            const char *lhs,
                            const char *rhs) {
    maelys_datalog_term_t terms[2];
    terms[0] = symbol_term(ruleset, lhs);
    terms[1] = symbol_term(ruleset, rhs);
    (void)maelys_datalog_edb_add_fact(edb, predicate, terms, 2u);
}

static maelys_result_t init_access_ruleset(maelys_datalog_ruleset_t *ruleset) {
    maelys_result_t rc = init_ruleset_base(ruleset, "determinism.access");
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "owns", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "blocked", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset,
                       "allow",
                       2u,
                       MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    static const char *const atoms[] = {"alice", "mallory", "doc.pdf", "bob", "other.pdf", NULL};
    for (size_t i = 0; atoms[i] != NULL; i++) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    const char *src =
        "blocked(\"mallory\").\n"
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n";
    return maelys_datalog_parse_ruleset(ruleset, src, strlen(src));
}

static maelys_result_t init_path_ruleset(maelys_datalog_ruleset_t *ruleset) {
    maelys_result_t rc = init_ruleset_base(ruleset, "determinism.path");
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset,
                       "path",
                       2u,
                       MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    static const char *const atoms[] = {"a", "b", "c", NULL};
    for (size_t i = 0; atoms[i] != NULL; i++) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    const char *src =
        "path(X, Y) :- edge(X, Y).\n"
        "path(X, Z) :- path(X, Y), edge(Y, Z).\n";
    return maelys_datalog_parse_ruleset(ruleset, src, strlen(src));
}

static maelys_result_t init_access_edb(maelys_datalog_ruleset_t *ruleset,
                                       maelys_datalog_edb_t *edb,
                                       maelys_datalog_fact_t *facts,
                                       size_t fact_capacity) {
    return maelys_datalog_edb_init(edb,
                                   facts,
                                   fact_capacity,
                                   &ruleset->symbols,
                                   &ruleset->registry);
}

static maelys_result_t query_symbol_pair(maelys_datalog_ruleset_t *ruleset,
                                         const maelys_datalog_solve_result_t *result,
                                         const char *predicate,
                                         const char *lhs,
                                         const char *rhs,
                                         bool *out_present) {
    maelys_datalog_term_t terms[2];
    terms[0] = symbol_term(ruleset, lhs);
    terms[1] = symbol_term(ruleset, rhs);
    return maelys_datalog_query_solved_ground_fact(result,
                                                   predicate,
                                                   terms,
                                                   2u,
                                                   out_present);
}

static int query_allow_pair(maelys_datalog_ruleset_t *ruleset,
                            const maelys_datalog_solve_result_t *result,
                            const char *user,
                            const char *doc,
                            bool *out_present) {
    return query_symbol_pair(ruleset, result, "allow", user, doc, out_present);
}

static int query_path_pair(maelys_datalog_ruleset_t *ruleset,
                           const maelys_datalog_solve_result_t *result,
                           const char *from,
                           const char *to,
                           bool *out_present) {
    return query_symbol_pair(ruleset, result, "path", from, to, out_present);
}

static int build_access_edb_with_alice_and_mallory(maelys_datalog_ruleset_t *ruleset,
                                                   maelys_datalog_edb_t *edb,
                                                   maelys_datalog_fact_t *facts,
                                                   size_t fact_capacity) {
    if (init_access_edb(ruleset, edb, facts, fact_capacity) != MAELYS_OK) return 0;
    add_symbol_pair(ruleset, edb, "owns", "alice", "doc.pdf");
    add_symbol_pair(ruleset, edb, "owns", "mallory", "doc.pdf");
    return maelys_datalog_edb_finalize(edb) == MAELYS_OK;
}

static int compare_facts_visible(const maelys_datalog_fact_t *lhs,
                                 const maelys_datalog_fact_t *rhs) {
    return maelys_datalog_fact_equals(lhs, rhs);
}

static int compare_proofs_visible(const maelys_datalog_proof_tree_t *lhs,
                                  const maelys_datalog_proof_tree_t *rhs) {
    if (!lhs || !rhs) return 0;
    if (strcmp(lhs->policy_id, rhs->policy_id) != 0) return 0;
    if (strcmp(lhs->sha256, rhs->sha256) != 0) return 0;
    if (lhs->node_count != rhs->node_count) return 0;
    if (lhs->truncated != rhs->truncated) return 0;
    if (lhs->verbose != rhs->verbose) return 0;
    for (size_t i = 0; i < lhs->node_count; i++) {
        const maelys_datalog_proof_node_t *a = &lhs->nodes[i];
        const maelys_datalog_proof_node_t *b = &rhs->nodes[i];
        if (a->rule_id != b->rule_id) return 0;
        if (a->predicate_id != b->predicate_id) return 0;
        if (a->deny_reason != b->deny_reason) return 0;
        if (a->depth != b->depth) return 0;
        if (a->parent_index != b->parent_index) return 0;
        if (!compare_facts_visible(&a->derived_fact, &b->derived_fact)) return 0;
    }
    return 1;
}

static maelys_result_t init_or_equivalence_ruleset(
    maelys_datalog_ruleset_t *ruleset,
    const char *source) {
    maelys_result_t rc = init_ruleset_base(ruleset, "determinism.or");
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "owns", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "delegated", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset, "blocked", 1u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = add_predicate(ruleset,
                       "allow",
                       2u,
                       MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    static const char *const atoms[] = {
        "alice", "bob", "mallory", "doc.pdf", NULL,
    };
    for (size_t i = 0; atoms[i] != NULL; i++) {
        rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, atoms[i]);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_parse_ruleset(ruleset, source, strlen(source));
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_ruleset_finalize_sha256(ruleset);
}

static maelys_result_t build_or_equivalence_edb(
    maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_edb_t *edb,
    maelys_datalog_fact_t *facts,
    size_t capacity) {
    maelys_result_t rc =
        maelys_datalog_edb_init(edb,
                                facts,
                                capacity,
                                &ruleset->symbols,
                                &ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    add_symbol_pair(ruleset, edb, "owns", "alice", "doc.pdf");
    add_symbol_pair(ruleset, edb, "delegated", "bob", "doc.pdf");
    maelys_datalog_term_t mallory = symbol_term(ruleset, "mallory");
    rc = maelys_datalog_edb_add_fact(edb, "blocked", &mallory, 1u);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_edb_finalize(edb);
}

static int test_determinism_or_matches_manual_rules_sha_results_and_proof(void) {
    TEST_BEGIN();
    static const char or_source[] =
        "allow(U, D) :- owns(U, D) or delegated(U, D), not(blocked(U)).\n";
    static const char manual_source[] =
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n"
        "allow(U, D) :- delegated(U, D), not(blocked(U)).\n";
    maelys_datalog_ruleset_t or_ruleset;
    maelys_datalog_ruleset_t manual_ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      init_or_equivalence_ruleset(&or_ruleset, or_source),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      init_or_equivalence_ruleset(&manual_ruleset, manual_source),
                      "%d");
    TEST_ASSERT_EQUAL(manual_ruleset.rule_count, or_ruleset.rule_count, "%zu");
    TEST_ASSERT_EQUAL_STRING(manual_ruleset.sha256, or_ruleset.sha256);
    for (size_t i = 0; i < or_ruleset.rule_count; i++) {
        TEST_ASSERT_EQUAL(i + 1u, or_ruleset.rules[i].rule_id, "%zu");
        TEST_ASSERT_EQUAL(0,
                          memcmp(&manual_ruleset.rules[i],
                                 &or_ruleset.rules[i],
                                 sizeof(or_ruleset.rules[i])),
                          "%d");
    }

    maelys_datalog_fact_t or_edb_facts[4];
    maelys_datalog_fact_t manual_edb_facts[4];
    maelys_datalog_edb_t or_edb;
    maelys_datalog_edb_t manual_edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      build_or_equivalence_edb(&or_ruleset,
                                               &or_edb,
                                               or_edb_facts,
                                               4u),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      build_or_equivalence_edb(&manual_ruleset,
                                               &manual_edb,
                                               manual_edb_facts,
                                               4u),
                      "%d");

    maelys_datalog_solve_result_t *or_result = NULL;
    maelys_datalog_solve_result_t *manual_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_once(&or_ruleset, &or_edb, &or_result),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_once(&manual_ruleset,
                                               &manual_edb,
                                               &manual_result),
                      "%d");
    size_t or_derived_count = 0;
    size_t manual_derived_count = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_derived_fact_count(
                          or_result,
                          &or_derived_count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_derived_fact_count(
                          manual_result,
                          &manual_derived_count),
                      "%d");
    TEST_ASSERT_EQUAL(manual_derived_count, or_derived_count, "%zu");

    maelys_datalog_fact_t or_facts[4];
    maelys_datalog_fact_t manual_facts[4];
    size_t or_count = 0;
    size_t manual_count = 0;
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        maelys_datalog_solve_result_enumerate_predicate_facts(
            or_result,
            "allow",
            2u,
            or_facts,
            4u,
            &or_count),
        "%d");
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        maelys_datalog_solve_result_enumerate_predicate_facts(
            manual_result,
            "allow",
            2u,
            manual_facts,
            4u,
            &manual_count),
        "%d");
    TEST_ASSERT_EQUAL(manual_count, or_count, "%zu");
    for (size_t i = 0; i < or_count; i++) {
        TEST_ASSERT_TRUE(compare_facts_visible(&manual_facts[i], &or_facts[i]));
    }

    const maelys_datalog_proof_tree_t *or_proof =
        maelys_datalog_solve_result_proof(or_result);
    const maelys_datalog_proof_tree_t *manual_proof =
        maelys_datalog_solve_result_proof(manual_result);
    TEST_ASSERT_NOT_NULL(or_proof);
    TEST_ASSERT_NOT_NULL(manual_proof);
    TEST_ASSERT_TRUE(compare_proofs_visible(manual_proof, or_proof));

    maelys_datalog_solve_result_free(or_result);
    maelys_datalog_solve_result_free(manual_result);
    maelys_datalog_ruleset_clear(&or_ruleset);
    maelys_datalog_ruleset_clear(&manual_ruleset);
    TEST_END();
}

static int test_determinism_repeated_solve_same_result(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_ruleset(&ruleset), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_TRUE(build_access_edb_with_alice_and_mallory(&ruleset,
                                                             &edb,
                                                             facts,
                                                             4u));

    maelys_datalog_solve_result_t *result1 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result1), "%d");
    bool alice1 = false;
    bool mallory1 = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result1, "alice", "doc.pdf", &alice1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result1, "mallory", "doc.pdf", &mallory1), "%d");
    TEST_ASSERT_TRUE(alice1);
    TEST_ASSERT_FALSE(mallory1);
    maelys_datalog_solve_result_free(result1);

    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result2), "%d");
    bool alice2 = false;
    bool mallory2 = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "alice", "doc.pdf", &alice2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "mallory", "doc.pdf", &mallory2), "%d");
    TEST_ASSERT_EQUAL(alice1, alice2, "%d");
    TEST_ASSERT_EQUAL(mallory1, mallory2, "%d");
    TEST_ASSERT_TRUE(alice2);
    TEST_ASSERT_FALSE(mallory2);
    maelys_datalog_solve_result_free(result2);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int solve_path_with_order(int reverse_order,
                                 maelys_datalog_ruleset_t *ruleset,
                                 maelys_datalog_edb_t *edb,
                                 maelys_datalog_fact_t *facts,
                                 size_t fact_capacity,
                                 maelys_datalog_solve_result_t **out_result) {
    if (init_path_ruleset(ruleset) != MAELYS_OK) return 0;
    if (maelys_datalog_edb_init(edb, facts, fact_capacity, &ruleset->symbols, &ruleset->registry) != MAELYS_OK) {
        return 0;
    }
    if (!reverse_order) {
        add_symbol_pair(ruleset, edb, "edge", "a", "b");
        add_symbol_pair(ruleset, edb, "edge", "b", "c");
        add_symbol_pair(ruleset, edb, "edge", "a", "c");
    } else {
        add_symbol_pair(ruleset, edb, "edge", "a", "c");
        add_symbol_pair(ruleset, edb, "edge", "b", "c");
        add_symbol_pair(ruleset, edb, "edge", "a", "b");
    }
    if (maelys_datalog_edb_finalize(edb) != MAELYS_OK) return 0;
    return maelys_datalog_solve_once(ruleset, edb, out_result) == MAELYS_OK;
}

static int test_determinism_edb_order_a_b_c(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_path_with_order(0, &ruleset, &edb, facts, 8u, &result));
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_path_pair(&ruleset, result, "a", "c", &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_edb_order_c_b_a(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_fact_t facts[8];
    maelys_datalog_edb_t edb;
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_TRUE(solve_path_with_order(1, &ruleset, &edb, facts, 8u, &result));
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_path_pair(&ruleset, result, "a", "c", &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_edb_order_compare(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset_a;
    maelys_datalog_ruleset_t ruleset_b;
    maelys_datalog_fact_t facts_a[8];
    maelys_datalog_fact_t facts_b[8];
    maelys_datalog_edb_t edb_a;
    maelys_datalog_edb_t edb_b;
    maelys_datalog_solve_result_t *result_a = NULL;
    maelys_datalog_solve_result_t *result_b = NULL;
    TEST_ASSERT_TRUE(solve_path_with_order(0, &ruleset_a, &edb_a, facts_a, 8u, &result_a));
    TEST_ASSERT_TRUE(solve_path_with_order(1, &ruleset_b, &edb_b, facts_b, 8u, &result_b));
    static const char *const lhs[] = {"a", "a", "b", "c"};
    static const char *const rhs[] = {"b", "c", "c", "a"};
    for (size_t i = 0; i < sizeof(lhs) / sizeof(lhs[0]); i++) {
        bool present_a = false;
        bool present_b = true;
        TEST_ASSERT_EQUAL(MAELYS_OK, query_path_pair(&ruleset_a, result_a, lhs[i], rhs[i], &present_a), "%d");
        TEST_ASSERT_EQUAL(MAELYS_OK, query_path_pair(&ruleset_b, result_b, lhs[i], rhs[i], &present_b), "%d");
        TEST_ASSERT_EQUAL(present_a, present_b, "%d");
    }
    maelys_datalog_solve_result_free(result_a);
    maelys_datalog_solve_result_free(result_b);
    maelys_datalog_ruleset_clear(&ruleset_a);
    maelys_datalog_ruleset_clear(&ruleset_b);
    TEST_END();
}

static int test_determinism_symbol_intern_idempotent(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_symbol_table_init(&symbols);
    maelys_datalog_symbol_id_t id1 = 0;
    maelys_datalog_symbol_id_t id2 = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, "alice", 5u, &id1), "%d");
    size_t count_after_first = symbols.count;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, "alice", 5u, &id2), "%d");
    TEST_ASSERT_EQUAL(id1, id2, "%u");
    TEST_ASSERT_EQUAL(count_after_first, symbols.count, "%zu");
    TEST_END();
}

static int test_determinism_symbol_intern_distinct_strings_distinct_ids(void) {
    TEST_BEGIN();
    maelys_datalog_symbol_table_t symbols;
    maelys_datalog_symbol_table_init(&symbols);
    maelys_datalog_symbol_id_t alice = 0;
    maelys_datalog_symbol_id_t bob = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, "alice", 5u, &alice), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_symbol_intern(&symbols, "bob", 3u, &bob), "%d");
    TEST_ASSERT_TRUE(alice != bob);
    TEST_ASSERT_EQUAL((size_t)2u, symbols.count, "%zu");
    TEST_END();
}

static int test_determinism_multi_request_isolation(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_ruleset(&ruleset), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_edb(&ruleset, &edb, facts, 4u), "%d");

    add_symbol_pair(&ruleset, &edb, "owns", "alice", "doc.pdf");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result1 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result1), "%d");
    bool present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result1, "alice", "doc.pdf", &present), "%d");
    TEST_ASSERT_TRUE(present);
    maelys_datalog_solve_result_free(result1);

    maelys_datalog_edb_clear(&edb);
    add_symbol_pair(&ruleset, &edb, "owns", "bob", "other.pdf");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result2), "%d");
    bool alice_present = true;
    bool bob_present = false;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "alice", "doc.pdf", &alice_present), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "bob", "other.pdf", &bob_present), "%d");
    TEST_ASSERT_FALSE(alice_present);
    TEST_ASSERT_TRUE(bob_present);
    maelys_datalog_solve_result_free(result2);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_same_edb_after_clear_reuse(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_ruleset(&ruleset), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_edb(&ruleset, &edb, facts, 4u), "%d");

    add_symbol_pair(&ruleset, &edb, "owns", "alice", "doc.pdf");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result1 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result1), "%d");
    bool alice1 = false;
    bool mallory1 = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result1, "alice", "doc.pdf", &alice1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result1, "mallory", "doc.pdf", &mallory1), "%d");
    maelys_datalog_solve_result_free(result1);

    maelys_datalog_edb_clear(&edb);
    add_symbol_pair(&ruleset, &edb, "owns", "alice", "doc.pdf");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result2), "%d");
    bool alice2 = false;
    bool mallory2 = true;
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "alice", "doc.pdf", &alice2), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, query_allow_pair(&ruleset, result2, "mallory", "doc.pdf", &mallory2), "%d");
    TEST_ASSERT_EQUAL(alice1, alice2, "%d");
    TEST_ASSERT_EQUAL(mallory1, mallory2, "%d");
    TEST_ASSERT_TRUE(alice2);
    TEST_ASSERT_FALSE(mallory2);
    maelys_datalog_solve_result_free(result2);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_proof_tree_node_count_stable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_ruleset(&ruleset), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_TRUE(build_access_edb_with_alice_and_mallory(&ruleset,
                                                             &edb,
                                                             facts,
                                                             4u));
    maelys_datalog_solve_result_t *result1 = NULL;
    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result1), "%d");
    const maelys_datalog_proof_tree_t *proof1 = maelys_datalog_solve_result_proof(result1);
    TEST_ASSERT_NOT_NULL(proof1);
    size_t node_count1 = proof1 ? proof1->node_count : 0u;
    maelys_datalog_solve_result_free(result1);
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result2), "%d");
    const maelys_datalog_proof_tree_t *proof2 = maelys_datalog_solve_result_proof(result2);
    TEST_ASSERT_NOT_NULL(proof2);
    TEST_ASSERT_EQUAL(node_count1, proof2 ? proof2->node_count : 0u, "%zu");
    maelys_datalog_solve_result_free(result2);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_proof_visible_metadata_stable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_access_ruleset(&ruleset), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_TRUE(build_access_edb_with_alice_and_mallory(&ruleset,
                                                             &edb,
                                                             facts,
                                                             4u));
    maelys_datalog_solve_result_t *result1 = NULL;
    maelys_datalog_solve_result_t *result2 = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result2), "%d");
    const maelys_datalog_proof_tree_t *proof1 = maelys_datalog_solve_result_proof(result1);
    const maelys_datalog_proof_tree_t *proof2 = maelys_datalog_solve_result_proof(result2);
    TEST_ASSERT_NOT_NULL(proof1);
    TEST_ASSERT_NOT_NULL(proof2);
    TEST_ASSERT_TRUE(compare_proofs_visible(proof1, proof2));
    maelys_datalog_solve_result_free(result1);
    maelys_datalog_solve_result_free(result2);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_determinism_c44_canonical_sha_fixture(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    memset(&ruleset, 0, sizeof(ruleset));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_ruleset_init(&ruleset,
                                                  "c44.canonical.fixture",
                                                  "c44",
                                                  MAELYS_DATALOG_SHA256_UNSET,
                                                  1),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      add_predicate(&ruleset, "owns", 2u, MAELYS_DATALOG_PRED_KIND_EDB),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      add_predicate(&ruleset, "blocked", 1u, MAELYS_DATALOG_PRED_KIND_POLICY_FACT),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      add_predicate(&ruleset,
                                    "allow",
                                    2u,
                                    MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY),
                      "%d");
    static const char *const atoms[] = {"alice", "mallory", "doc.pdf", NULL};
    for (size_t i = 0; atoms[i] != NULL; i++) {
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_predicate_registry_add_atom(&ruleset.registry, atoms[i]),
                          "%d");
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_predicate_registry_freeze(&ruleset.registry), "%d");
    const char *src =
        "blocked(\"mallory\").\n"
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n";
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_parse_ruleset(&ruleset, src, strlen(src)), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_ruleset_finalize_sha256(&ruleset), "%d");
    TEST_ASSERT_EQUAL_STRING("e745887b8f197efa9b91c29b15dcdccd436aa44f481e9ab17f9926c521be5834",
                             ruleset.sha256);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

/* P4-C64 — off-stack (file-static) explanation buffers per §3.6/§6.2. */
static maelys_datalog_explanation_t g_det_exp_a;
static maelys_datalog_explanation_t g_det_exp_b;

static int det_make_fact2(maelys_datalog_ruleset_t *ruleset,
                          const char *predicate,
                          const char *a,
                          const char *b,
                          maelys_datalog_fact_t *out) {
    memset(out, 0, sizeof(*out));
    if (!maelys_datalog_predicate_registry_find(&ruleset->registry, predicate, 2u, &out->predicate_id)) {
        return 0;
    }
    out->arity = 2u;
    out->terms[0] = symbol_term(ruleset, a);
    out->terms[1] = symbol_term(ruleset, b);
    return 1;
}

/* §6.1(14): an OR source and its manual expansion yield byte-identical
 * Why-true explanations. */
static int test_determinism_or_and_manual_explanation_byte_identical(void) {
    TEST_BEGIN();
    static const char or_source[] =
        "allow(U, D) :- owns(U, D) or delegated(U, D), not(blocked(U)).\n";
    static const char manual_source[] =
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n"
        "allow(U, D) :- delegated(U, D), not(blocked(U)).\n";
    maelys_datalog_ruleset_t or_ruleset;
    maelys_datalog_ruleset_t manual_ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_or_equivalence_ruleset(&or_ruleset, or_source), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, init_or_equivalence_ruleset(&manual_ruleset, manual_source), "%d");

    maelys_datalog_fact_t or_facts[4];
    maelys_datalog_fact_t manual_facts[4];
    maelys_datalog_edb_t or_edb;
    maelys_datalog_edb_t manual_edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, build_or_equivalence_edb(&or_ruleset, &or_edb, or_facts, 4u), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, build_or_equivalence_edb(&manual_ruleset, &manual_edb, manual_facts, 4u), "%d");

    maelys_datalog_solve_result_t *or_result = NULL;
    maelys_datalog_solve_result_t *manual_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&or_ruleset, &or_edb, &or_result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&manual_ruleset, &manual_edb, &manual_result), "%d");

    maelys_datalog_fact_t or_target;
    maelys_datalog_fact_t manual_target;
    TEST_ASSERT_TRUE(det_make_fact2(&or_ruleset, "allow", "alice", "doc.pdf", &or_target));
    TEST_ASSERT_TRUE(det_make_fact2(&manual_ruleset, "allow", "alice", "doc.pdf", &manual_target));

    /* Different prior bytes must not leak through padding or inactive unions. */
    memset(&g_det_exp_a, 0xa5, sizeof(g_det_exp_a));
    memset(&g_det_exp_b, 0x5a, sizeof(g_det_exp_b));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(or_result, &or_target, &g_det_exp_a), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(manual_result, &manual_target, &g_det_exp_b), "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, g_det_exp_a.found, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, g_det_exp_a.truncated, "%u");
    TEST_ASSERT_EQUAL(0, memcmp(&g_det_exp_a, &g_det_exp_b, sizeof(g_det_exp_a)), "%d");

    maelys_datalog_solve_result_free(or_result);
    maelys_datalog_solve_result_free(manual_result);
    maelys_datalog_ruleset_clear(&or_ruleset);
    maelys_datalog_ruleset_clear(&manual_ruleset);
    TEST_END();
}

/* P4-C65 §6.4: an OR source and its manual expansion yield byte-identical
 * MAELYS-DATALOG-WHY-TRUE-TEXT-v1 renderings (off-stack text buffers). */
static char g_det_text_a[8192];
static char g_det_text_b[8192];

static int test_determinism_or_and_manual_explanation_text_byte_identical(void) {
    TEST_BEGIN();
    static const char or_source[] =
        "allow(U, D) :- owns(U, D) or delegated(U, D), not(blocked(U)).\n";
    static const char manual_source[] =
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n"
        "allow(U, D) :- delegated(U, D), not(blocked(U)).\n";
    maelys_datalog_ruleset_t or_ruleset;
    maelys_datalog_ruleset_t manual_ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, init_or_equivalence_ruleset(&or_ruleset, or_source), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, init_or_equivalence_ruleset(&manual_ruleset, manual_source), "%d");

    maelys_datalog_fact_t or_facts[4];
    maelys_datalog_fact_t manual_facts[4];
    maelys_datalog_edb_t or_edb;
    maelys_datalog_edb_t manual_edb;
    TEST_ASSERT_EQUAL(MAELYS_OK, build_or_equivalence_edb(&or_ruleset, &or_edb, or_facts, 4u), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, build_or_equivalence_edb(&manual_ruleset, &manual_edb, manual_facts, 4u), "%d");

    maelys_datalog_solve_result_t *or_result = NULL;
    maelys_datalog_solve_result_t *manual_result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&or_ruleset, &or_edb, &or_result), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&manual_ruleset, &manual_edb, &manual_result), "%d");

    maelys_datalog_fact_t or_target;
    maelys_datalog_fact_t manual_target;
    TEST_ASSERT_TRUE(det_make_fact2(&or_ruleset, "allow", "alice", "doc.pdf", &or_target));
    TEST_ASSERT_TRUE(det_make_fact2(&manual_ruleset, "allow", "alice", "doc.pdf", &manual_target));

    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(or_result, &or_target, &g_det_exp_a), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(manual_result, &manual_target, &g_det_exp_b), "%d");
    TEST_ASSERT_EQUAL((uint8_t)1u, g_det_exp_a.found, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0u, g_det_exp_a.truncated, "%u");

    size_t or_required = 0u;
    size_t manual_required = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(&or_ruleset,
                                                             &g_det_exp_a,
                                                             g_det_text_a,
                                                             sizeof(g_det_text_a),
                                                             &or_required),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_format_explanation_text(&manual_ruleset,
                                                             &g_det_exp_b,
                                                             g_det_text_b,
                                                             sizeof(g_det_text_b),
                                                             &manual_required),
                      "%d");
    TEST_ASSERT_EQUAL(or_required, manual_required, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(g_det_text_a, g_det_text_b, or_required + 1u), "%d");

    maelys_datalog_solve_result_free(or_result);
    maelys_datalog_solve_result_free(manual_result);
    maelys_datalog_ruleset_clear(&or_ruleset);
    maelys_datalog_ruleset_clear(&manual_ruleset);
    TEST_END();
}

/* §6.3: the production (static join order) path yields a deterministic witness
 * across repeated independent solves. */
static int test_determinism_explanation_stable_across_repeated_solves(void) {
    TEST_BEGIN();
    static const char source[] =
        "allow(U, D) :- owns(U, D), not(blocked(U)).\n";
    for (int iteration = 0; iteration < 2; iteration++) {
        maelys_datalog_ruleset_t ruleset;
        TEST_ASSERT_EQUAL(MAELYS_OK, init_or_equivalence_ruleset(&ruleset, source), "%d");
        maelys_datalog_fact_t facts[4];
        maelys_datalog_edb_t edb;
        TEST_ASSERT_EQUAL(MAELYS_OK, build_or_equivalence_edb(&ruleset, &edb, facts, 4u), "%d");
        maelys_datalog_solve_result_t *result = NULL;
        TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
        maelys_datalog_fact_t target;
        TEST_ASSERT_TRUE(det_make_fact2(&ruleset, "allow", "alice", "doc.pdf", &target));
        maelys_datalog_explanation_t *slot = (iteration == 0) ? &g_det_exp_a : &g_det_exp_b;
        memset(slot, (iteration == 0) ? 0xa5 : 0x5a, sizeof(*slot));
        TEST_ASSERT_EQUAL(MAELYS_OK,
                          maelys_datalog_explain_solved_fact(result, &target, slot), "%d");
        TEST_ASSERT_EQUAL((uint8_t)1u, slot->found, "%u");
        TEST_ASSERT_EQUAL((uint8_t)0u, slot->truncated, "%u");
        maelys_datalog_solve_result_free(result);
        maelys_datalog_ruleset_clear(&ruleset);
    }
    TEST_ASSERT_EQUAL(0, memcmp(&g_det_exp_a, &g_det_exp_b, sizeof(g_det_exp_a)), "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_determinism/repeated_solve_same_result",
         TEST_MODE_NON_BLOCKING,
         test_determinism_repeated_solve_same_result},
        {"maelys_datalog_determinism/edb_order_a_b_c",
         TEST_MODE_NON_BLOCKING,
         test_determinism_edb_order_a_b_c},
        {"maelys_datalog_determinism/edb_order_c_b_a",
         TEST_MODE_NON_BLOCKING,
         test_determinism_edb_order_c_b_a},
        {"maelys_datalog_determinism/edb_order_compare",
         TEST_MODE_NON_BLOCKING,
         test_determinism_edb_order_compare},
        {"maelys_datalog_determinism/symbol_intern_idempotent",
         TEST_MODE_NON_BLOCKING,
         test_determinism_symbol_intern_idempotent},
        {"maelys_datalog_determinism/symbol_intern_distinct_strings_distinct_ids",
         TEST_MODE_NON_BLOCKING,
         test_determinism_symbol_intern_distinct_strings_distinct_ids},
        {"maelys_datalog_determinism/multi_request_isolation",
         TEST_MODE_NON_BLOCKING,
         test_determinism_multi_request_isolation},
        {"maelys_datalog_determinism/same_edb_after_clear_reuse",
         TEST_MODE_NON_BLOCKING,
         test_determinism_same_edb_after_clear_reuse},
        {"maelys_datalog_determinism/proof_tree_node_count_stable",
         TEST_MODE_NON_BLOCKING,
         test_determinism_proof_tree_node_count_stable},
        {"maelys_datalog_determinism/proof_visible_metadata_stable",
         TEST_MODE_NON_BLOCKING,
         test_determinism_proof_visible_metadata_stable},
        {"maelys_datalog_determinism/c44_canonical_sha_fixture",
         TEST_MODE_NON_BLOCKING,
         test_determinism_c44_canonical_sha_fixture},
        {"maelys_datalog_determinism/or_matches_manual_rules_sha_results_and_proof",
         TEST_MODE_NON_BLOCKING,
         test_determinism_or_matches_manual_rules_sha_results_and_proof},
        {"maelys_datalog_determinism/or_and_manual_explanation_byte_identical",
         TEST_MODE_NON_BLOCKING,
         test_determinism_or_and_manual_explanation_byte_identical},
        {"maelys_datalog_determinism/explanation_stable_across_repeated_solves",
         TEST_MODE_NON_BLOCKING,
         test_determinism_explanation_stable_across_repeated_solves},
        {"maelys_datalog_determinism/or_and_manual_explanation_text_byte_identical",
         TEST_MODE_NON_BLOCKING,
         test_determinism_or_and_manual_explanation_text_byte_identical},
    };
    return test_main("maelys_datalog_determinism",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
