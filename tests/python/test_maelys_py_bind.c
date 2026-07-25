#include "bindings/python/maelys_py_bind.h"

#include <stdint.h>
#include <string.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "tests/helpers/test_framework.h"

static const maelys_py_predicate_def_t k_predicates[] = {
    {"edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
    {"path", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
};

static int register_loop_domain(void) {
    return maelys_py_register_domain("py_bind_c_loop", k_predicates, 2u);
}

static maelys_result_t install_callback_domain(maelys_datalog_predicate_registry_t *registry) {
    (void)registry;
    return MAELYS_OK;
}

static int exercise_success_once(void) {
    TEST_BEGIN();

    maelys_py_engine_t *engine = NULL;
    maelys_py_ruleset_t *ruleset = NULL;
    maelys_py_edb_t *edb = NULL;
    maelys_py_result_t *result = NULL;

    int call_rc = register_loop_domain();
    TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");

    engine = maelys_py_engine_new();
    TEST_ASSERT_NOT_NULL(engine);

    if (engine) {
        const char *policy =
            "path(X, Y) :- edge(X, Y).\n"
            "path(X, Z) :- edge(X, Y), path(Y, Z).";
        call_rc = maelys_py_load_inline_ruleset(engine,
                                                "py_bind_c_loop",
                                                "policy",
                                                policy,
                                                strlen(policy),
                                                &ruleset);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_NOT_NULL(ruleset);
    }

    if (ruleset) {
        edb = maelys_py_edb_new(ruleset);
        TEST_ASSERT_NOT_NULL(edb);
    }

    uint32_t a = 0u, b = 0u, c = 0u;
    if (ruleset) {
        call_rc = maelys_py_intern_symbol(ruleset, "a", &a);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        call_rc = maelys_py_intern_symbol(ruleset, "b", &b);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        call_rc = maelys_py_intern_symbol(ruleset, "c", &c);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(a != 0u);
        TEST_ASSERT_TRUE(b != 0u);
        TEST_ASSERT_TRUE(c != 0u);

        uint32_t lookup_id = 99u;
        int found = 0;
        call_rc = maelys_py_symbol_lookup_readonly(
            ruleset, "a", strlen("a"), &lookup_id, &found);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(found);
        TEST_ASSERT_EQUAL(a, lookup_id, "%u");
        call_rc = maelys_py_symbol_lookup_readonly(
            ruleset, "never-interned", strlen("never-interned"), &lookup_id, &found);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_FALSE(found);
        TEST_ASSERT_EQUAL((uint32_t)MAELYS_DATALOG_SYMBOL_ID_INVALID, lookup_id, "%u");

        int valid = 0;
        call_rc = maelys_py_symbol_id_is_valid(ruleset, a, &valid);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(valid);
        call_rc = maelys_py_symbol_id_is_valid(ruleset, UINT32_MAX, &valid);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_FALSE(valid);
    }

    if (edb) {
        maelys_py_term_t edge_ab[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)a},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)b},
        };
        maelys_py_term_t edge_bc[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)b},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)c},
        };
        call_rc = maelys_py_edb_add_fact(edb, "edge", edge_ab, 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        call_rc = maelys_py_edb_add_fact(edb, "edge", edge_bc, 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
    }

    if (ruleset && edb) {
        call_rc = maelys_py_solve(ruleset, edb, &result);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_NOT_NULL(result);
    }

    size_t count = 0u;
    if (result) {
        call_rc = maelys_py_result_derived_fact_count(result, &count);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");

        call_rc = maelys_py_result_enumerate_predicate_facts(
            result, "path", 2u, NULL, 0u, &count);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");

        maelys_py_term_t terms[6];
        memset(terms, 0, sizeof(terms));
        call_rc = maelys_py_result_enumerate_predicate_facts(
            result, "path", 2u, terms, 3u, &count);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_EQUAL((size_t)3u, count, "%zu");

        if (ruleset) {
            const char *text = maelys_py_symbol_text(ruleset, (uint32_t)terms[0].value);
            TEST_ASSERT_NOT_NULL(text);
            if (text) TEST_ASSERT_TRUE(text[0] != '\0');
        }

        call_rc = maelys_py_result_validate_query_predicate(result, "path", 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        call_rc = maelys_py_result_validate_query_predicate(result, "edge", 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        call_rc = maelys_py_result_validate_query_predicate(result, "missing", 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        call_rc = maelys_py_result_validate_query_predicate(NULL, "path", 2u);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_ARGUMENT, call_rc, "%d");

        maelys_py_term_t path_ac[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)a},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)c},
        };
        int present = 0;
        call_rc = maelys_py_result_contains_fact(
            result, "path", path_ac, 2u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(present);

        maelys_py_term_t path_ca[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)c},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)a},
        };
        call_rc = maelys_py_result_contains_fact(
            result, "path", path_ca, 2u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_FALSE(present);

        call_rc = maelys_py_result_contains_fact(
            result, "path", NULL, 2u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_ARGUMENT, call_rc, "%d");
        call_rc = maelys_py_result_contains_fact(
            result, "path", NULL, 0u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        maelys_py_term_t oversized_terms[MAELYS_DATALOG_MAX_ARITY + 1u];
        memset(oversized_terms, 0, sizeof(oversized_terms));
        present = 1;
        call_rc = maelys_py_result_contains_fact(
            result,
            "path",
            oversized_terms,
            MAELYS_DATALOG_MAX_ARITY + 1u,
            &present);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(present);
        present = 1;
        call_rc = maelys_py_result_contains_fact(
            NULL, "path", path_ac, 2u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_ARGUMENT, call_rc, "%d");
        TEST_ASSERT_FALSE(present);
    }

    maelys_py_result_free(result);
    maelys_py_edb_free(edb);
    maelys_py_ruleset_free(ruleset);
    maelys_py_engine_free(engine);
    TEST_END();
}

static int test_maelys_py_bind_reuses_domain_slot_1000_times(void) {
    TEST_BEGIN();
    for (size_t i = 0u; i < 1000u; i++) {
        int call_rc = register_loop_domain();
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
    }
    TEST_END();
}

static int test_maelys_py_bind_success_solve_enumerate_and_free(void) {
    return exercise_success_once();
}

static int test_maelys_py_bind_load_failure_diag_and_free(void) {
    TEST_BEGIN();

    int call_rc = register_loop_domain();
    TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");

    maelys_py_engine_t *engine = maelys_py_engine_new();
    TEST_ASSERT_NOT_NULL(engine);

    maelys_py_ruleset_t *ruleset = NULL;
    if (engine) {
        const char *bad_policy = "path(X, Y) :- missing(X, Y).";
        call_rc = maelys_py_load_inline_ruleset(engine,
                                                "py_bind_c_loop",
                                                "bad",
                                                bad_policy,
                                                strlen(bad_policy),
                                                &ruleset);
        TEST_ASSERT_TRUE(call_rc != (int)MAELYS_OK);
        TEST_ASSERT_NULL(ruleset);
        const char *message = maelys_py_last_diag_message(engine);
        TEST_ASSERT_NOT_NULL(message);
        if (message) TEST_ASSERT_TRUE(message[0] != '\0');
    }

    maelys_py_ruleset_free(ruleset);
    maelys_py_engine_free(engine);
    TEST_END();
}

static int test_maelys_py_bind_noninspectable_domain_fails_closed(void) {
    TEST_BEGIN();

    const maelys_datalog_domain_def_t callback_domain = {
        "py_bind_callback_domain",
        NULL,
        0u,
        "callback-backed domain for Python binding tests",
        install_callback_domain,
    };
    int call_rc = (int)maelys_datalog_domain_registry_register(&callback_domain);
    TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");

    maelys_py_predicate_def_t predicates[1];
    size_t count = 123u;
    int found = 0;
    int inspectable = 1;
    call_rc = maelys_py_find_domain("py_bind_callback_domain",
                                    predicates,
                                    1u,
                                    &count,
                                    &found,
                                    &inspectable);
    TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_FALSE(inspectable);
    TEST_ASSERT_EQUAL((size_t)0u, count, "%zu");

    call_rc = maelys_py_register_domain("py_bind_callback_domain", k_predicates, 2u);
    TEST_ASSERT_EQUAL((int)MAELYS_ERR_UNSUPPORTED, call_rc, "%d");

    TEST_END();
}

int main(int argc, char **argv) {
    tests_init_logging();
    const test_case_t cases[] = {
        {"maelys_py_bind/reuses_domain_slot_1000_times",
         TEST_MODE_NON_BLOCKING,
         test_maelys_py_bind_reuses_domain_slot_1000_times},
        {"maelys_py_bind/success_solve_enumerate_and_free",
         TEST_MODE_NON_BLOCKING,
         test_maelys_py_bind_success_solve_enumerate_and_free},
        {"maelys_py_bind/load_failure_diag_and_free",
         TEST_MODE_NON_BLOCKING,
         test_maelys_py_bind_load_failure_diag_and_free},
        {"maelys_py_bind/noninspectable_domain_fails_closed",
         TEST_MODE_NON_BLOCKING,
         test_maelys_py_bind_noninspectable_domain_fails_closed},
    };
    return test_main("maelys_py_bind",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
