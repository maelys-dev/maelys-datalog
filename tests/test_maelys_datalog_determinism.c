#include "src/core/maelys_datalog_edb.h"
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
    };
    return test_main("maelys_datalog_determinism",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
