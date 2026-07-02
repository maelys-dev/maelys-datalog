#include "include/maelys_datalog.h"
#include "tests/helpers/test_framework.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *const k_test_sha =
    "0000000000000000000000000000000000000000000000000000000000000000";

static maelys_result_t make_ruleset(maelys_datalog_ruleset_t *ruleset,
                                    const char *policy) {
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
        MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_parse_ruleset(ruleset, policy, strlen(policy));
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
