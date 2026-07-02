#include "include/maelys_datalog.h"
#include "tests/helpers/test_framework.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *const k_test_sha =
    "0000000000000000000000000000000000000000000000000000000000000000";

static maelys_result_t make_ruleset_with_path_kind(maelys_datalog_ruleset_t *ruleset,
                                                   const char *policy,
                                                   uint32_t path_kind_flags) {
    if (!ruleset) return MAELYS_ERR_INVALID_ARGUMENT;
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = maelys_datalog_ruleset_init(
        ruleset, "policy.statistics", "graph", k_test_sha, 1);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry, "edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_domain(
        &ruleset->registry,
        "path",
        2u,
        path_kind_flags);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_parse_ruleset(ruleset, policy, strlen(policy));
}

static maelys_result_t make_ruleset(maelys_datalog_ruleset_t *ruleset,
                                    const char *policy) {
    return make_ruleset_with_path_kind(
        ruleset,
        policy,
        MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
}

static maelys_datalog_term_t symbol_term(maelys_datalog_ruleset_t *ruleset,
                                         const char *text) {
    maelys_datalog_symbol_id_t id = 0;
    (void)maelys_datalog_symbol_intern(&ruleset->symbols, text, strlen(text), &id);
    maelys_datalog_term_t term = {.kind = MAELYS_DATALOG_TERM_SYMBOL};
    term.as.symbol = id;
    return term;
}

static maelys_result_t add_edge(maelys_datalog_ruleset_t *ruleset,
                                maelys_datalog_edb_t *edb,
                                const char *left,
                                const char *right) {
    maelys_datalog_term_t terms[2] = {
        symbol_term(ruleset, left),
        symbol_term(ruleset, right),
    };
    return maelys_datalog_edb_add_fact(edb, "edge", terms, 2u);
}

static int fact_pair_matches(const maelys_datalog_fact_t *fact,
                             maelys_datalog_symbol_id_t left,
                             maelys_datalog_symbol_id_t right) {
    return fact &&
           fact->arity == 2u &&
           fact->terms[0].kind == MAELYS_DATALOG_TERM_SYMBOL &&
           fact->terms[1].kind == MAELYS_DATALOG_TERM_SYMBOL &&
           fact->terms[0].as.symbol == left &&
           fact->terms[1].as.symbol == right;
}

static int facts_contain_pair(const maelys_datalog_fact_t *facts,
                              size_t count,
                              maelys_datalog_ruleset_t *ruleset,
                              const char *left,
                              const char *right) {
    const maelys_datalog_symbol_id_t left_id = symbol_term(ruleset, left).as.symbol;
    const maelys_datalog_symbol_id_t right_id = symbol_term(ruleset, right).as.symbol;
    for (size_t i = 0u; i < count; i++) {
        if (fact_pair_matches(&facts[i], left_id, right_id)) return 1;
    }
    return 0;
}

static maelys_result_t solve_path_fixture(maelys_datalog_ruleset_t *ruleset,
                                          maelys_datalog_edb_t *edb,
                                          maelys_datalog_fact_t *edb_facts,
                                          size_t edb_capacity,
                                          maelys_datalog_solve_result_t **out_result) {
    const char *policy =
        "path(X, Y) :- edge(X, Y).\n"
        "path(X, Z) :- edge(X, Y), path(Y, Z).";
    maelys_result_t rc = make_ruleset(ruleset, policy);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_edb_init(edb, edb_facts, edb_capacity, &ruleset->symbols, &ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    rc = add_edge(ruleset, edb, "a", "b");
    if (rc != MAELYS_OK) return rc;
    rc = add_edge(ruleset, edb, "b", "c");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_edb_finalize(edb);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_solve_once(ruleset, edb, out_result);
}

static int test_derived_fact_count_success_and_purity(void) {
    TEST_BEGIN();
    const char *policy =
        "path(X, Y) :- edge(X, Y).\n"
        "path(X, Z) :- edge(X, Y), path(Y, Z).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, policy), "%d");
    maelys_datalog_fact_t facts[4];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 4u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_edge(&ruleset, &edb, "a", "b"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_edge(&ruleset, &edb, "b", "c"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    size_t first = 0u;
    size_t second = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_derived_fact_count(result, &first),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)3u, first, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_derived_fact_count(result, &second),
                      "%d");
    TEST_ASSERT_EQUAL(first, second, "%zu");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_success_set_semantics(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[3];
    size_t count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 3u, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");
    TEST_ASSERT_TRUE(facts_contain_pair(out, 3u, &ruleset, "a", "b"));
    TEST_ASSERT_TRUE(facts_contain_pair(out, 3u, &ruleset, "b", "c"));
    TEST_ASSERT_TRUE(facts_contain_pair(out, 3u, &ruleset, "a", "c"));

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_empty_query_relation(void) {
    TEST_BEGIN();
    const char *policy = "path(X, Y) :- edge(X, Y).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, policy), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 1u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[1];
    size_t count = 1234u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 1u, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, count, "%zu");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_truncation_reports_total(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[1];
    size_t count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 1u, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");
    TEST_ASSERT_TRUE(facts_contain_pair(out, 1u, &ruleset, "a", "b") ||
                     facts_contain_pair(out, 1u, &ruleset, "b", "c") ||
                     facts_contain_pair(out, 1u, &ruleset, "a", "c"));

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_count_only_mode(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    size_t count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, NULL, 0u, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_rejects_non_query_predicate(void) {
    TEST_BEGIN();
    const char *policy = "path(X, Y) :- edge(X, Y).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset_with_path_kind(&ruleset,
                                                  policy,
                                                  MAELYS_DATALOG_PRED_KIND_IDB),
                      "%d");
    maelys_datalog_fact_t facts[2];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 2u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, add_edge(&ruleset, &edb, "a", "b"), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[1];
    size_t count = 999u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 1u, &count),
                      "%d");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_rejects_absent_predicate(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[1];
    size_t count = 999u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "missing", 2u, out, 1u, &count),
                      "%d");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_enforces_query_whitelist(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);
    ruleset.enforces_query_whitelist = 1;
    ruleset.query_whitelist_count = 1u;
    snprintf(ruleset.query_whitelist[0].name,
             sizeof(ruleset.query_whitelist[0].name),
             "%s",
             "other");
    ruleset.query_whitelist[0].arity = 2u;

    maelys_datalog_fact_t out[1];
    size_t count = 999u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_FORBIDDEN,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 1u, &count),
                      "%d");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_invalid_arguments(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t out[1];
    size_t count = 999u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          NULL, "path", 2u, out, 1u, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)999u, count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, NULL, 2u, out, 1u, &count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, NULL, 1u, &count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, out, 1u, NULL),
                      "%d");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_enumerate_predicate_facts_read_only_purity(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    maelys_datalog_edb_t edb;
    maelys_datalog_fact_t edb_facts[4];
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_path_fixture(&ruleset, &edb, edb_facts, 4u, &result),
                      "%d");
    TEST_ASSERT_NOT_NULL(result);

    maelys_datalog_fact_t first[3];
    maelys_datalog_fact_t second[3];
    size_t first_count = 0u;
    size_t second_count = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, first, 3u, &first_count),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_enumerate_predicate_facts(
                          result, "path", 2u, second, 3u, &second_count),
                      "%d");
    TEST_ASSERT_EQUAL(first_count, second_count, "%zu");
    TEST_ASSERT_EQUAL(0, memcmp(first, second, sizeof(first)), "%d");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_derived_fact_count_zero_is_valid(void) {
    TEST_BEGIN();
    const char *policy = "path(X, Y) :- edge(X, Y).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, policy), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 1u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    size_t count = 1234u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_derived_fact_count(result, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)0u, count, "%zu");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_derived_fact_count_null_arguments_preserve_output(void) {
    TEST_BEGIN();
    size_t count = 1234u;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_derived_fact_count(NULL, &count),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1234u, count, "%zu");

    const char *policy = "path(X, Y) :- edge(X, Y).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, policy), "%d");
    maelys_datalog_fact_t facts[1];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 1u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_solve_once(&ruleset, &edb, &result), "%d");
    TEST_ASSERT_NOT_NULL(result);

    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_derived_fact_count(result, NULL),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)1234u, count, "%zu");

    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int test_failed_solve_does_not_publish_result(void) {
    TEST_BEGIN();
    const char *policy =
        "path(X, Y) :- edge(X, Y).\n"
        "path(X, Z) :- path(X, Y), edge(Y, Z).";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, policy), "%d");
    maelys_datalog_fact_t facts[16];
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(&edb, facts, 16u, &ruleset.symbols, &ruleset.registry),
                      "%d");
    for (size_t i = 0u; i < MAELYS_DATALOG_MAX_DEPTH + 1u; i++) {
        char left[16];
        char right[16];
        snprintf(left, sizeof(left), "n%zu", i);
        snprintf(right, sizeof(right), "n%zu", i + 1u);
        TEST_ASSERT_EQUAL(MAELYS_OK, add_edge(&ruleset, &edb, left, right), "%d");
    }
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");

    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_solve_once(&ruleset, &edb, &result),
                      "%d");
    TEST_ASSERT_NULL(result);

    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

int main(int argc, char **argv) {
    const test_case_t tests[] = {
        {"maelys_datalog_solver_statistics/derived_fact_count_success_and_purity",
         TEST_MODE_BLOCKING,
         test_derived_fact_count_success_and_purity},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_success_set_semantics",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_success_set_semantics},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_empty_query_relation",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_empty_query_relation},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_truncation_reports_total",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_truncation_reports_total},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_count_only_mode",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_count_only_mode},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_rejects_non_query_predicate",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_rejects_non_query_predicate},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_rejects_absent_predicate",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_rejects_absent_predicate},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_enforces_query_whitelist",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_enforces_query_whitelist},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_invalid_arguments",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_invalid_arguments},
        {"maelys_datalog_solver_statistics/enumerate_predicate_facts_read_only_purity",
         TEST_MODE_BLOCKING,
         test_enumerate_predicate_facts_read_only_purity},
        {"maelys_datalog_solver_statistics/derived_fact_count_zero_is_valid",
         TEST_MODE_BLOCKING,
         test_derived_fact_count_zero_is_valid},
        {"maelys_datalog_solver_statistics/derived_fact_count_null_arguments_preserve_output",
         TEST_MODE_BLOCKING,
         test_derived_fact_count_null_arguments_preserve_output},
        {"maelys_datalog_solver_statistics/failed_solve_does_not_publish_result",
         TEST_MODE_BLOCKING,
         test_failed_solve_does_not_publish_result},
    };
    return test_main("maelys_datalog_solver_statistics",
                     tests,
                     (int)(sizeof(tests) / sizeof(tests[0])),
                     argc,
                     argv);
}
