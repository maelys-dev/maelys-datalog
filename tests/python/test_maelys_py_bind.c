#include "bindings/python/maelys_py_bind.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/maelys_errors.h"
#include "src/core/maelys_datalog_domain_registry.h"
#include "tests/helpers/test_framework.h"

static const maelys_py_predicate_def_t k_predicates[] = {
    {"edge", 2u, MAELYS_DATALOG_PRED_KIND_EDB},
    {"path", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
    {"reach", 2u, MAELYS_DATALOG_PRED_KIND_IDB | MAELYS_DATALOG_PRED_KIND_QUERY},
};

static int register_loop_domain(void) {
    return maelys_py_register_domain("py_bind_c_loop", k_predicates, 3u);
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

        maelys_py_term_t path_ab[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)a},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)b},
        };
        static const char expected_explanation[] =
            "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
            "status=complete\n"
            "steps=1 premises=1\n"
            "step=0 rule=1 fact=\"path\"(\"a\",\"b\")\n"
            "premise=0 body=0 kind=positive origin=edb "
            "fact=\"edge\"(\"a\",\"b\") parent=-\n"
            "result-step=0\n";
        size_t required = 99u;
        int found_explanation = 0;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", path_ab, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(found_explanation);
        TEST_ASSERT_EQUAL(sizeof(expected_explanation) - 1u, required, "%zu");

        char *explanation_text = (char *)malloc(required + 1u);
        TEST_ASSERT_NOT_NULL(explanation_text);
        if (explanation_text) {
            size_t written_required = 0u;
            int written_found = 0;
            call_rc = maelys_py_result_explain_fact_text(
                result, "path", path_ab, 2u, explanation_text, required + 1u,
                &written_required, &written_found);
            TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
            TEST_ASSERT_TRUE(written_found);
            TEST_ASSERT_EQUAL(required, written_required, "%zu");
            TEST_ASSERT_EQUAL(
                0,
                memcmp(explanation_text, expected_explanation, required + 1u),
                "%d");
            free(explanation_text);
        }

        char insufficient[8];
        memset(insufficient, 0x5a, sizeof(insufficient));
        size_t insufficient_required = 0u;
        found_explanation = 0;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", path_ab, 2u, insufficient, sizeof(insufficient),
            &insufficient_required, &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_PAYLOAD_TOO_LARGE, call_rc, "%d");
        TEST_ASSERT_TRUE(found_explanation);
        TEST_ASSERT_EQUAL(required, insufficient_required, "%zu");
        TEST_ASSERT_EQUAL('\0', insufficient[0], "%d");

        maelys_py_term_t path_ca[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)c},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)a},
        };
        call_rc = maelys_py_result_contains_fact(
            result, "path", path_ca, 2u, &present);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_FALSE(present);

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", path_ca, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

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

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            NULL, "path", path_ac, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_ARGUMENT, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result, "edge", path_ab, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result, "missing", path_ab, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", path_ab, 1u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

        maelys_py_term_t invalid_terms[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_VAR, 0},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)b},
        };
        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", invalid_terms, 2u, NULL, 0u, &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");

        required = 99u;
        found_explanation = 1;
        call_rc = maelys_py_result_explain_fact_text(
            result,
            "path",
            oversized_terms,
            MAELYS_DATALOG_MAX_ARITY + 1u,
            NULL,
            0u,
            &required,
            &found_explanation);
        TEST_ASSERT_EQUAL((int)MAELYS_ERR_INVALID_FIELD, call_rc, "%d");
        TEST_ASSERT_FALSE(found_explanation);
        TEST_ASSERT_EQUAL((size_t)0u, required, "%zu");
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

static int test_maelys_py_bind_truncated_explanation_is_not_absent(void) {
    TEST_BEGIN();

    int call_rc = register_loop_domain();
    TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");

    maelys_py_engine_t *engine = maelys_py_engine_new();
    TEST_ASSERT_NOT_NULL(engine);
    maelys_py_ruleset_t *ruleset = NULL;
    maelys_py_edb_t *edb = NULL;
    maelys_py_result_t *result = NULL;

    if (engine) {
        const char *policy =
            "path(X, Y) :- edge(X, Y).\n"
            "path(X, Z) :- path(X, Y), edge(Y, Z).\n"
            "reach(X, Y) :- path(X, Y).";
        call_rc = maelys_py_load_inline_ruleset(engine,
                                                "py_bind_c_loop",
                                                "truncated-policy",
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

    uint32_t nodes[9] = {0u};
    if (ruleset && edb) {
        static const char *const names[9] = {
            "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8",
        };
        for (size_t i = 0u; i < 9u; i++) {
            call_rc = maelys_py_intern_symbol(ruleset, names[i], &nodes[i]);
            TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
            TEST_ASSERT_TRUE(nodes[i] != 0u);
        }
        for (size_t i = 0u; i < 8u; i++) {
            maelys_py_term_t edge[2] = {
                {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)nodes[i]},
                {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)nodes[i + 1u]},
            };
            call_rc = maelys_py_edb_add_fact(edb, "edge", edge, 2u);
            TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        }
        call_rc = maelys_py_solve(ruleset, edb, &result);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_NOT_NULL(result);
    }

    if (result) {
        maelys_py_term_t target[2] = {
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)nodes[0]},
            {(int32_t)MAELYS_DATALOG_TERM_SYMBOL, (int64_t)nodes[8]},
        };
        static const char expected[] =
            "MAELYS-DATALOG-WHY-TRUE-TEXT-v1\n"
            "status=truncated\n"
            "steps=0 premises=0\n";
        size_t required = 0u;
        int found = 0;
        call_rc = maelys_py_result_explain_fact_text(
            result, "path", target, 2u, NULL, 0u, &required, &found);
        TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
        TEST_ASSERT_TRUE(found);
        TEST_ASSERT_EQUAL(sizeof(expected) - 1u, required, "%zu");

        char *text = (char *)malloc(required + 1u);
        TEST_ASSERT_NOT_NULL(text);
        if (text) {
            size_t written_required = 0u;
            int written_found = 0;
            call_rc = maelys_py_result_explain_fact_text(
                result, "path", target, 2u, text, required + 1u,
                &written_required, &written_found);
            TEST_ASSERT_EQUAL((int)MAELYS_OK, call_rc, "%d");
            TEST_ASSERT_TRUE(written_found);
            TEST_ASSERT_EQUAL(required, written_required, "%zu");
            TEST_ASSERT_EQUAL(0, memcmp(text, expected, sizeof(expected)), "%d");
            free(text);
        }
    }

    maelys_py_result_free(result);
    maelys_py_edb_free(edb);
    maelys_py_ruleset_free(ruleset);
    maelys_py_engine_free(engine);
    TEST_END();
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
        {"maelys_py_bind/truncated_explanation_is_not_absent",
         TEST_MODE_NON_BLOCKING,
         test_maelys_py_bind_truncated_explanation_is_not_absent},
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
