#include "include/maelys_datalog.h"
#include "src/core/maelys_datalog_filter.h"
#include "tests/helpers/test_framework.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static maelys_result_t make_ruleset(maelys_datalog_ruleset_t *ruleset,
                                    const char *source,
                                    int homonym) {
    memset(ruleset, 0, sizeof(*ruleset));
    maelys_result_t rc = maelys_datalog_ruleset_init(
        ruleset, "filters.test", "filters", MAELYS_DATALOG_SHA256_UNSET, 1);
    if (rc != MAELYS_OK) return rc;
    static const maelys_datalog_predicate_def_t defs[] = {
        {"ref", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"ref2", 1u, MAELYS_DATALOG_PRED_KIND_EDB},
        {"start", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"ending", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
        {"inside", 1u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    };
    for (size_t i = 0u; i < sizeof(defs) / sizeof(defs[0]); i++) {
        rc = maelys_datalog_predicate_registry_add_domain(
            &ruleset->registry, defs[i].name, defs[i].arity, defs[i].kind_flags);
        if (rc != MAELYS_OK) return rc;
    }
    if (homonym) {
        rc = maelys_datalog_predicate_registry_add_domain(
            &ruleset->registry,
            "starts_with",
            2u,
            MAELYS_DATALOG_PRED_KIND_EDB);
        if (rc != MAELYS_OK) return rc;
    }
    rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, "alice");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_add_atom(&ruleset->registry, "pattern");
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_predicate_registry_freeze(&ruleset->registry);
    if (rc != MAELYS_OK) return rc;
    rc = maelys_datalog_parse_ruleset_ex(
        ruleset, source, strlen(source), "filters.dl", NULL);
    if (rc != MAELYS_OK) return rc;
    return maelys_datalog_ruleset_finalize_sha256(ruleset);
}

static maelys_result_t solve_ref(maelys_datalog_ruleset_t *ruleset,
                                 const char *value,
                                 maelys_datalog_solve_result_t **out_result) {
    maelys_datalog_fact_t *pool = calloc(
        MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*pool));
    if (!pool) return MAELYS_ERR_INTERNAL;
    maelys_datalog_edb_t edb;
    maelys_result_t rc = maelys_datalog_edb_init(
        &edb,
        pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &ruleset->symbols,
        &ruleset->registry);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_edb_add_runtime_symbol_fact(&edb, "ref", value);
    }
    if (rc == MAELYS_OK) rc = maelys_datalog_edb_finalize(&edb);
    if (rc == MAELYS_OK) rc = maelys_datalog_solve_once(ruleset, &edb, out_result);
    maelys_datalog_edb_clear(&edb);
    free(pool);
    return rc;
}

static maelys_result_t solve_ref_sets(
    maelys_datalog_ruleset_t *ruleset,
    size_t value_count,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    if (value_count > MAELYS_DATALOG_MAX_FACTS_PER_PRED) {
        return MAELYS_ERR_INVALID_ARGUMENT;
    }
    maelys_datalog_fact_t *pool = calloc(
        MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*pool));
    if (!pool) return MAELYS_ERR_INTERNAL;
    char left[MAELYS_DATALOG_MAX_FACTS_PER_PRED][16];
    char right[MAELYS_DATALOG_MAX_FACTS_PER_PRED][16];
    const char *left_values[MAELYS_DATALOG_MAX_FACTS_PER_PRED];
    const char *right_values[MAELYS_DATALOG_MAX_FACTS_PER_PRED];
    for (size_t i = 0u; i < value_count; i++) {
        (void)snprintf(left[i], sizeof(left[i]), "left-%zu", i);
        (void)snprintf(right[i], sizeof(right[i]), "right-%zu", i);
        left_values[i] = left[i];
        right_values[i] = right[i];
    }
    maelys_datalog_edb_t edb;
    maelys_result_t rc = maelys_datalog_edb_init(
        &edb,
        pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &ruleset->symbols,
        &ruleset->registry);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_edb_add_runtime_symbol_facts(
            &edb, "ref", left_values, value_count);
    }
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_edb_add_runtime_symbol_facts(
            &edb, "ref2", right_values, value_count);
    }
    if (rc == MAELYS_OK) rc = maelys_datalog_edb_finalize(&edb);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_solve_once_ex(
            ruleset, &edb, out_result, out_diag);
    }
    maelys_datalog_edb_clear(&edb);
    free(pool);
    return rc;
}

static maelys_result_t solve_empty(
    maelys_datalog_ruleset_t *ruleset,
    maelys_datalog_solve_result_t **out_result,
    maelys_datalog_solve_diagnostic_t *out_diag) {
    maelys_datalog_fact_t *pool = calloc(
        MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*pool));
    if (!pool) return MAELYS_ERR_INTERNAL;
    maelys_datalog_edb_t edb;
    maelys_result_t rc = maelys_datalog_edb_init(
        &edb,
        pool,
        MAELYS_DATALOG_MAX_EDB_FACTS,
        &ruleset->symbols,
        &ruleset->registry);
    if (rc == MAELYS_OK) rc = maelys_datalog_edb_finalize(&edb);
    if (rc == MAELYS_OK) {
        rc = maelys_datalog_solve_once_ex(
            ruleset, &edb, out_result, out_diag);
    }
    maelys_datalog_edb_clear(&edb);
    free(pool);
    return rc;
}

static int result_has(const maelys_datalog_solve_result_t *result,
                      const char *predicate,
                      maelys_datalog_fact_t *out_fact) {
    size_t count = 0u;
    if (maelys_datalog_solve_result_enumerate_predicate_facts(
            result, predicate, 1u, out_fact, out_fact ? 1u : 0u, &count) !=
        MAELYS_OK) return 0;
    return count == 1u;
}

static int parser_forms_and_pattern_pool(void) {
    TEST_BEGIN();
    const char *source =
        "start(R) :- starts_with(R, \"refs/heads/\"), ref(R).\n"
        "ending(R) :- ref(R), ends_with(R, \"/main\").\n"
        "inside(R) :- ref(R), contains(R, \"é\").\n";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    TEST_ASSERT_EQUAL((size_t)3u, ruleset.filter_program_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_LITERAL_FILTER,
                      ruleset.rules[0].body[0].kind, "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_LITERAL_ATOM,
                      ruleset.rules[0].body[1].kind, "%d");
    TEST_ASSERT_TRUE(ruleset.filter_pattern_pool_used > 0u);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int parser_ground_safety_and_types(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- starts_with(R, \"x\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- ref(R), starts_with(1, \"x\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- ref(R), starts_with(R, R).",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- ref(R), starts_with(R, true).",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- ref(R), starts_with(R, \"x\", \"y\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "starts_with(\"alice\", \"a\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "starts_with(R, \"a\") :- ref(R).",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      make_ruleset(&ruleset,
                                   "start(R) :- ref(R), unknown_filter(R, \"a\").",
                                   0), "%d");
    TEST_END();
}

static int parser_registry_homonym_wins(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        make_ruleset(&ruleset,
                     "start(R) :- starts_with(R, \"pattern\").",
                     1), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_LITERAL_ATOM,
                      ruleset.rules[0].body[0].kind, "%d");
    TEST_ASSERT_EQUAL((size_t)0u, ruleset.filter_program_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int parser_pattern_is_not_an_atom_or_symbol(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    const char *source =
        "start(R) :- ref(R), starts_with(R, \"not-in-vocabulary\").";
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    TEST_ASSERT_EQUAL((size_t)0u, ruleset.symbols.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)2u, ruleset.registry.atom_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)1u, ruleset.filter_program_count, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int parser_capacity_refusal_is_atomic(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, "", 0), "%d");
    ruleset.filter_program_count = MAELYS_DATALOG_MAX_FILTER_PROGRAMS;
    maelys_datalog_ruleset_t before = ruleset;
    const char *source =
        "start(R) :- ref(R), starts_with(R, \"x\").";
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_parse_ruleset(
                          &ruleset, source, strlen(source)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&before, &ruleset, sizeof(ruleset)), "%d");
    maelys_datalog_ruleset_clear(&ruleset);

    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, "", 0), "%d");
    ruleset.filter_program_count = MAELYS_DATALOG_MAX_FILTER_PROGRAMS;
    before = ruleset;
    const char *constant_value_source =
        "start(R) :- ref(R), starts_with(\"alice\", \"x\").";
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_parse_ruleset(
                          &ruleset,
                          constant_value_source,
                          strlen(constant_value_source)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&before, &ruleset, sizeof(ruleset)), "%d");
    maelys_datalog_ruleset_clear(&ruleset);

    char long_pattern[MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES + 2u];
    memset(long_pattern, 'x', sizeof(long_pattern));
    long_pattern[sizeof(long_pattern) - 1u] = '\0';
    char long_source[512];
    int written = snprintf(long_source,
                           sizeof(long_source),
                           "start(R) :- ref(R), starts_with(\"alice\", \"%s\").",
                           long_pattern);
    TEST_ASSERT_TRUE(written > 0);
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, "", 0), "%d");
    before = ruleset;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_parse_ruleset(
                          &ruleset, long_source, strlen(long_source)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&before, &ruleset, sizeof(ruleset)), "%d");
    maelys_datalog_ruleset_clear(&ruleset);

    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, "", 0), "%d");
    ruleset.filter_pattern_pool_used =
        MAELYS_DATALOG_FILTER_PATTERN_POOL_BYTES;
    before = ruleset;
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_parse_ruleset(
                          &ruleset,
                          constant_value_source,
                          strlen(constant_value_source)), "%d");
    TEST_ASSERT_EQUAL(0, memcmp(&before, &ruleset, sizeof(ruleset)), "%d");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int semantics_ascii_utf8_empty_and_case(void) {
    TEST_BEGIN();
    int matched = -1;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_STARTS_WITH,
                          (const unsigned char *)"refs/heads/main",
                          strlen("refs/heads/main"),
                          (const unsigned char *)"refs/",
                          strlen("refs/"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_STARTS_WITH,
                          (const unsigned char *)"Refs/heads/main",
                          strlen("Refs/heads/main"),
                          (const unsigned char *)"refs/",
                          strlen("refs/"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(!matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_ENDS_WITH,
                          (const unsigned char *)"café",
                          strlen("café"),
                          (const unsigned char *)"fé",
                          strlen("fé"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_ENDS_WITH,
                          (const unsigned char *)"short",
                          strlen("short"),
                          (const unsigned char *)"too-long",
                          strlen("too-long"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(!matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_CONTAINS,
                          (const unsigned char *)"préfixe-café-suffixe",
                          strlen("préfixe-café-suffixe"),
                          (const unsigned char *)"café",
                          strlen("café"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_CONTAINS,
                          (const unsigned char *)"abcdef",
                          strlen("abcdef"),
                          (const unsigned char *)"ACE",
                          strlen("ACE"),
                          &matched), "%d");
    TEST_ASSERT_TRUE(!matched);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_CONTAINS,
                          (const unsigned char *)"anything",
                          strlen("anything"),
                          (const unsigned char *)"",
                          0u,
                          &matched), "%d");
    TEST_ASSERT_TRUE(matched);
    const unsigned char with_nul[] = {'a', 0u, 'b'};
    const unsigned char nul_suffix[] = {0u, 'b'};
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_evaluate(
                          MAELYS_DATALOG_FILTER_ENDS_WITH,
                          with_nul,
                          sizeof(with_nul),
                          nul_suffix,
                          sizeof(nul_suffix),
                          &matched), "%d");
    TEST_ASSERT_TRUE(matched);

    const char *source =
        "start(R) :- starts_with(R, \"refs/\"), ref(R).\n"
        "ending(R) :- ref(R), ends_with(R, \"42\").\n"
        "inside(R) :- ref(R), contains(R, \"é\").\n";
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_ref(&ruleset, "refs/café/42", &result), "%d");
    maelys_datalog_fact_t fact;
    TEST_ASSERT_TRUE(result_has(result, "start", &fact));
    TEST_ASSERT_TRUE(result_has(result, "ending", &fact));
    TEST_ASSERT_TRUE(result_has(result, "inside", &fact));
    maelys_datalog_solve_result_free(result);

    maelys_datalog_filter_statistics_t statistics;
    memset(&statistics, 0xA5, sizeof(statistics));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_ARGUMENT,
                      maelys_datalog_solve_result_filter_statistics(
                          NULL, &statistics), "%d");
    TEST_ASSERT_EQUAL((unsigned char)0xA5,
                      ((unsigned char *)&statistics)[0], "%u");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int planner_filter_before_lier_is_deferred(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(
                          &ruleset,
                          "start(R) :- starts_with(R, \"refs/\"), ref(R).",
                          0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_ref(&ruleset, "refs/heads/main", &result), "%d");
    maelys_datalog_fact_t fact;
    TEST_ASSERT_TRUE(result_has(result, "start", &fact));
    maelys_datalog_explanation_t *explanation =
        calloc(1u, sizeof(*explanation));
    TEST_ASSERT_NOT_NULL(explanation);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_solved_fact(
                          result, &fact, explanation), "%d");
    TEST_ASSERT_TRUE(explanation->found);
    TEST_ASSERT_EQUAL((uint16_t)0u,
                      explanation->premises[0].body_index, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_EXPLANATION_PREMISE_FILTER_TRUE,
                      explanation->premises[0].kind, "%u");
    free(explanation);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int statistics_exact_and_cost_formula(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(
                          &ruleset,
                          "start(R) :- ref(R), starts_with(R, \"refs/\").\n"
                          "ending(R) :- ref(R), ends_with(R, \"no\").\n"
                          "inside(R) :- ref(R), contains(R, \"heads\").",
                          0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_ref(&ruleset, "refs/heads/main", &result), "%d");
    maelys_datalog_filter_statistics_t statistics;
    memset(&statistics, 0, sizeof(statistics));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_filter_statistics(
                          result, &statistics), "%d");
    TEST_ASSERT_EQUAL((size_t)3u, statistics.evaluations, "%zu");
    TEST_ASSERT_EQUAL((size_t)2u, statistics.matches, "%zu");
    TEST_ASSERT_EQUAL((size_t)1u, statistics.non_matches, "%zu");
    TEST_ASSERT_EQUAL((size_t)(5u + 2u + 15u * 5u),
                      statistics.cost_units, "%zu");
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int bounds_public_constants_and_checked_cost(void) {
    TEST_BEGIN();
    size_t cost = 0u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_cost(
                          MAELYS_DATALOG_FILTER_STARTS_WITH, 1024u, 256u, &cost),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)256u, cost, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_filter_cost(
                          MAELYS_DATALOG_FILTER_CONTAINS, 1024u, 256u, &cost),
                      "%d");
    TEST_ASSERT_EQUAL((size_t)262144u, cost, "%zu");
    TEST_ASSERT_EQUAL((size_t)4096u,
                      (size_t)MAELYS_DATALOG_MAX_FILTER_COST_UNITS / 256u, "%zu");
    TEST_ASSERT_TRUE(MAELYS_DATALOG_MAX_FILTER_EVALUATIONS > 4096u);
    TEST_END();
}

static int bounds_cost_last_admitted_then_refused(void) {
    TEST_BEGIN();
    char pattern[MAELYS_DATALOG_MAX_FILTER_PATTERN_BYTES + 1u];
    char value[MAELYS_DATALOG_MAX_STRING_BYTES + 1u];
    memset(pattern, 'b', sizeof(pattern) - 1u);
    pattern[sizeof(pattern) - 1u] = '\0';
    memset(value, 'a', sizeof(value) - 1u);
    value[sizeof(value) - 1u] = '\0';

    char clause[512];
    int written = snprintf(clause,
                           sizeof(clause),
                           "start(R) :- ref(R), contains(R, \"%s\").\n",
                           pattern);
    TEST_ASSERT_TRUE(written > 0);
    char source[4096];
    source[0] = '\0';
    for (size_t i = 0u; i < 4u; i++) {
        TEST_ASSERT_TRUE(strlen(source) + strlen(clause) < sizeof(source));
        (void)strcat(source, clause);
    }

    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK, solve_ref(&ruleset, value, &result), "%d");
    maelys_datalog_filter_statistics_t statistics;
    memset(&statistics, 0, sizeof(statistics));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_filter_statistics(
                          result, &statistics), "%d");
    TEST_ASSERT_EQUAL((size_t)4u, statistics.evaluations, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FILTER_COST_UNITS,
                      statistics.cost_units, "%zu");
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);

    TEST_ASSERT_TRUE(strlen(source) + strlen(clause) < sizeof(source));
    (void)strcat(source, clause);
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    maelys_datalog_fact_t *pool = calloc(
        MAELYS_DATALOG_MAX_EDB_FACTS, sizeof(*pool));
    TEST_ASSERT_NOT_NULL(pool);
    maelys_datalog_edb_t edb;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_init(
                          &edb,
                          pool,
                          MAELYS_DATALOG_MAX_EDB_FACTS,
                          &ruleset.symbols,
                          &ruleset.registry), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_edb_add_runtime_symbol_fact(
                          &edb, "ref", value), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK, maelys_datalog_edb_finalize(&edb), "%d");
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      maelys_datalog_solve_once_ex(
                          &ruleset, &edb, &result, &diag), "%d");
    TEST_ASSERT_TRUE(result == NULL);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                      diag.category, "%u");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_DENY_FILTER_ERROR,
                      diag.failure_reason, "%u");
    maelys_datalog_edb_clear(&edb);
    free(pool);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int bounds_evaluations_last_admitted_then_refused(void) {
    TEST_BEGIN();
    const size_t facts_per_predicate = 64u;
    TEST_ASSERT_EQUAL(
        (size_t)MAELYS_DATALOG_MAX_FILTER_EVALUATIONS,
        (size_t)2u * facts_per_predicate * facts_per_predicate,
        "%zu");
    const char *clause =
        "start(R) :- ref(R), ref2(S), starts_with(R, \"never-match\").\n";
    char source[512];
    int written = snprintf(source, sizeof(source), "%s%s", clause, clause);
    TEST_ASSERT_TRUE(written > 0);
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_ref_sets(
                          &ruleset,
                          facts_per_predicate,
                          &result,
                          &diag), "%d");
    maelys_datalog_filter_statistics_t statistics;
    memset(&statistics, 0, sizeof(statistics));
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_solve_result_filter_statistics(
                          result, &statistics), "%d");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FILTER_EVALUATIONS,
                      statistics.evaluations, "%zu");
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);

    written = snprintf(source,
                       sizeof(source),
                       "%s%s%s",
                       clause,
                       clause,
                       clause);
    TEST_ASSERT_TRUE(written > 0);
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, source, 0), "%d");
    result = NULL;
    memset(&diag, 0, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_ERR_PAYLOAD_TOO_LARGE,
                      solve_ref_sets(
                          &ruleset,
                          facts_per_predicate,
                          &result,
                          &diag), "%d");
    TEST_ASSERT_TRUE(result == NULL);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                      diag.category, "%u");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int bounds_invalid_unused_program_is_refused(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(
                          &ruleset,
                          "start(R) :- ref(R), starts_with(R, \"refs/\").",
                          0), "%d");
    ruleset.filter_programs[0].pattern_offset =
        (uint32_t)(ruleset.filter_pattern_pool_used + 1u);
    maelys_datalog_solve_result_t *result = NULL;
    maelys_datalog_solve_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      solve_empty(&ruleset, &result, &diag), "%d");
    TEST_ASSERT_TRUE(result == NULL);
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_SOLVE_DIAG_FILTER_ERROR,
                      diag.category, "%u");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int fingerprint_pattern_and_kind_change_identity(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t first;
    maelys_datalog_ruleset_t second;
    maelys_datalog_ruleset_t third;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&first,
                                   "start(R) :- ref(R), starts_with(R, \"a\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&second,
                                   "start(R) :- ref(R), starts_with(R, \"b\").",
                                   0), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&third,
                                   "start(R) :- ref(R), contains(R, \"a\").",
                                   0), "%d");
    TEST_ASSERT_TRUE(strcmp(first.sha256, second.sha256) != 0);
    TEST_ASSERT_TRUE(strcmp(first.sha256, third.sha256) != 0);
    TEST_ASSERT_EQUAL_STRING(
        "48110270d97783980847aa652502e3adcdb4a39753177880b7b52237ab6acfc7",
        first.sha256);

    maelys_datalog_ruleset_t ordered;
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        make_ruleset(
            &ordered,
            "start(R) :- ref(R), starts_with(R, \"a\").\n"
            "ending(R) :- ref(R), ends_with(R, \"z\").",
            0),
        "%d");
    maelys_datalog_ruleset_t reordered = ordered;
    const maelys_datalog_filter_program_t saved_program =
        reordered.filter_programs[0];
    reordered.filter_programs[0] = reordered.filter_programs[1];
    reordered.filter_programs[1] = saved_program;
    for (size_t rule = 0u; rule < reordered.rule_count; rule++) {
        for (size_t body = 0u;
             body < reordered.rules[rule].body_count;
             body++) {
            maelys_datalog_literal_t *literal =
                &reordered.rules[rule].body[body];
            if (literal->kind == MAELYS_DATALOG_LITERAL_FILTER) {
                literal->filter_program_index =
                    (uint16_t)(1u - literal->filter_program_index);
            }
        }
    }
    char saved_atom[sizeof(reordered.registry.atoms[0])];
    memcpy(saved_atom,
           reordered.registry.atoms[0],
           sizeof(saved_atom));
    memcpy(reordered.registry.atoms[0],
           reordered.registry.atoms[1],
           sizeof(saved_atom));
    memcpy(reordered.registry.atoms[1],
           saved_atom,
           sizeof(saved_atom));
    (void)strcpy(reordered.sha256, MAELYS_DATALOG_SHA256_UNSET);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_ruleset_finalize_sha256(&reordered), "%d");
    TEST_ASSERT_EQUAL_STRING(ordered.sha256, reordered.sha256);
    maelys_datalog_ruleset_clear(&ordered);
    maelys_datalog_ruleset_clear(&reordered);
    maelys_datalog_ruleset_clear(&first);
    maelys_datalog_ruleset_clear(&second);
    maelys_datalog_ruleset_clear(&third);
    TEST_END();
}

static int pod_ruleset_is_value_copyable(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t first;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(&first,
                                   "start(R) :- ref(R), starts_with(R, \"refs/\").",
                                   0), "%d");
    maelys_datalog_ruleset_t second = first;
    TEST_ASSERT_EQUAL(0, memcmp(&first, &second, sizeof(first)), "%d");
    maelys_datalog_ruleset_clear(&first);
    TEST_ASSERT_EQUAL((uint8_t)MAELYS_DATALOG_FILTER_STARTS_WITH,
                      second.filter_programs[0].kind, "%u");
    maelys_datalog_ruleset_clear(&second);
    TEST_END();
}

static int check_filter_why_false(const char *source) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      make_ruleset(
                          &ruleset,
                          source,
                          0), "%d");
    maelys_datalog_solve_result_t *result = NULL;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      solve_ref(&ruleset, "refs/tags/v1", &result), "%d");
    maelys_datalog_symbol_id_t symbol = 0u;
    int found = 0;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_symbol_lookup_readonly(
                          &ruleset.symbols,
                          "refs/tags/v1",
                          strlen("refs/tags/v1"),
                          &symbol,
                          &found), "%d");
    TEST_ASSERT_TRUE(found);
    maelys_datalog_predicate_id_t pid = 0u;
    TEST_ASSERT_TRUE(maelys_datalog_predicate_registry_find(
        &ruleset.registry, "start", 1u, &pid));
    maelys_datalog_fact_t query;
    memset(&query, 0, sizeof(query));
    query.predicate_id = pid;
    query.arity = 1u;
    query.terms[0].kind = MAELYS_DATALOG_TERM_SYMBOL;
    query.terms[0].as.symbol = symbol;
    maelys_datalog_why_false_limits_t limits = {
        MAELYS_DATALOG_MAX_RULES,
        MAELYS_DATALOG_MAX_WHY_FALSE_SUBSTITUTIONS_PER_RULE,
        MAELYS_DATALOG_MAX_PROOF_DEPTH,
        MAELYS_DATALOG_MAX_WHY_FALSE_DIAGNOSTICS,
    };
    maelys_datalog_why_false_explanation_t *why = calloc(1u, sizeof(*why));
    TEST_ASSERT_NOT_NULL(why);
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_explain_absent_solved_fact(
                          result, &query, &limits, why), "%d");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_STATUS_COMPLETE,
                      why->status, "%u");
    TEST_ASSERT_EQUAL((size_t)1u, why->diagnostic_count, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_WHY_FALSE_OBSTACLE_FILTER_FALSE,
                      why->diagnostics[0].obstacle.kind, "%u");
    TEST_ASSERT_TRUE(why->filter_cost_units > 0u);
    free(why);
    maelys_datalog_solve_result_free(result);
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

static int explanation_filter_true_and_false_are_typed(void) {
    return check_filter_why_false("start(R) :- ref(R), starts_with(R, \"refs/heads/\").");
}
static int external_filter_why_false(void) {
    return check_filter_why_false("start(R) :- ref(R), never_match(R, \"x\").");
}
static maelys_datalog_status_t module_validate(const unsigned char *p, size_t n) {
    return n == 6u && memcmp(p, "reject", 6u) == 0
        ? MAELYS_DATALOG_STATUS_INVALID_FIELD : MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t module_cost(size_t v, size_t p, size_t *out) {
    (void)v; (void)p; *out = 1u; return MAELYS_DATALOG_STATUS_OK;
}
static maelys_datalog_status_t module_false(
    const unsigned char *v, size_t vn, const unsigned char *p, size_t pn, int *out) {
    (void)v; (void)vn; (void)p; (void)pn; *out = 0; return MAELYS_DATALOG_STATUS_OK;
}
static int module_rejection_is_atomic(void) {
    TEST_BEGIN();
    maelys_datalog_ruleset_t ruleset;
    TEST_ASSERT_EQUAL(MAELYS_OK, make_ruleset(&ruleset, "start(R) :- ref(R).", 0), "%d");
    size_t count = ruleset.rule_count, symbols = ruleset.symbols.count;
    const char source[] = "start(R) :- ref(R), never_match(R, \"reject\").";
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_FIELD,
                      maelys_datalog_parse_ruleset(&ruleset, source, strlen(source)), "%d");
    TEST_ASSERT_EQUAL(count, ruleset.rule_count, "%zu");
    TEST_ASSERT_EQUAL(symbols, ruleset.symbols.count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, ruleset.filter_program_count, "%zu");
    TEST_ASSERT_EQUAL((size_t)0u, ruleset.filter_pattern_pool_used, "%zu");
    maelys_datalog_ruleset_clear(&ruleset);
    TEST_END();
}

int main(int argc, char **argv) {
    const maelys_datalog_filter_module_t module = {
        1u, sizeof(maelys_datalog_filter_module_t), "never_match", "test.never-match.v1",
        module_validate, module_cost, module_false};
    if (maelys_datalog_register_filter_module(&module) != MAELYS_DATALOG_STATUS_OK) return 1;
    const test_case_t cases[] = {
        {"filters/parser_forms_and_pattern_pool", TEST_MODE_NON_BLOCKING, parser_forms_and_pattern_pool},
        {"filters/parser_ground_safety_and_types", TEST_MODE_NON_BLOCKING, parser_ground_safety_and_types},
        {"filters/parser_registry_homonym_wins", TEST_MODE_NON_BLOCKING, parser_registry_homonym_wins},
        {"filters/parser_pattern_is_not_an_atom_or_symbol", TEST_MODE_NON_BLOCKING, parser_pattern_is_not_an_atom_or_symbol},
        {"filters/parser_capacity_refusal_is_atomic", TEST_MODE_NON_BLOCKING, parser_capacity_refusal_is_atomic},
        {"filters/semantics_ascii_utf8_empty_and_case", TEST_MODE_NON_BLOCKING, semantics_ascii_utf8_empty_and_case},
        {"filters/planner_filter_before_lier_is_deferred", TEST_MODE_NON_BLOCKING, planner_filter_before_lier_is_deferred},
        {"filters/statistics_exact_and_cost_formula", TEST_MODE_NON_BLOCKING, statistics_exact_and_cost_formula},
        {"filters/bounds_public_constants_and_checked_cost", TEST_MODE_NON_BLOCKING, bounds_public_constants_and_checked_cost},
        {"filters/bounds_cost_last_admitted_then_refused", TEST_MODE_NON_BLOCKING, bounds_cost_last_admitted_then_refused},
        {"filters/bounds_evaluations_last_admitted_then_refused", TEST_MODE_NON_BLOCKING, bounds_evaluations_last_admitted_then_refused},
        {"filters/bounds_invalid_unused_program_is_refused", TEST_MODE_NON_BLOCKING, bounds_invalid_unused_program_is_refused},
        {"filters/fingerprint_pattern_and_kind_change_identity", TEST_MODE_NON_BLOCKING, fingerprint_pattern_and_kind_change_identity},
        {"filters/pod_ruleset_is_value_copyable", TEST_MODE_NON_BLOCKING, pod_ruleset_is_value_copyable},
        {"filters/explanation_filter_true_and_false_are_typed", TEST_MODE_NON_BLOCKING, explanation_filter_true_and_false_are_typed},
        {"filters/external_filter_why_false", TEST_MODE_NON_BLOCKING, external_filter_why_false},
        {"filters/module_rejection_is_atomic", TEST_MODE_NON_BLOCKING, module_rejection_is_atomic},
    };
    return test_main("maelys_datalog_filters",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
