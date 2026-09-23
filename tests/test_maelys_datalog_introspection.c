#include "include/maelys_datalog.h"
#include <maelys/datalog.h>
#include "tests/helpers/test_framework.h"

#include <stddef.h>

static int test_build_limits_match_compile_time_macros(void) {
    TEST_BEGIN();

    const struct { maelys_datalog_limit_t key; size_t expected; } public_limits[] = {
        {MAELYS_DATALOG_LIMIT_MAX_SYMBOLS, MAELYS_DATALOG_MAX_SYMBOLS},
        {MAELYS_DATALOG_LIMIT_STRING_POOL_BYTES, MAELYS_DATALOG_STRING_POOL_BYTES},
        {MAELYS_DATALOG_LIMIT_MAX_PREDICATES, MAELYS_DATALOG_MAX_PREDICATES},
        {MAELYS_DATALOG_LIMIT_MAX_RULES, MAELYS_DATALOG_MAX_RULES},
        {MAELYS_DATALOG_LIMIT_MAX_ARITY, MAELYS_DATALOG_MAX_ARITY},
        {MAELYS_DATALOG_LIMIT_MAX_BODY_LITERALS, MAELYS_DATALOG_MAX_BODY_LITERALS},
        {MAELYS_DATALOG_LIMIT_MAX_DEPTH, MAELYS_DATALOG_MAX_DEPTH},
        {MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, MAELYS_DATALOG_MAX_EDB_FACTS},
        {MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS, MAELYS_DATALOG_MAX_IDB_FACTS},
        {MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, MAELYS_DATALOG_MAX_FACTS_PER_PRED},
        {MAELYS_DATALOG_LIMIT_MAX_STRING_BYTES, MAELYS_DATALOG_MAX_STRING_BYTES},
    };
    for (size_t i = 0u; i < sizeof(public_limits) / sizeof(public_limits[0]); ++i) {
        size_t actual = 0u;
        TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_OK,
                          maelys_datalog_limit_get(public_limits[i].key, &actual), "%d");
        TEST_ASSERT_EQUAL(public_limits[i].expected, actual, "%zu");
    }

    size_t untouched = 73u;
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_UNSUPPORTED,
        maelys_datalog_limit_get((maelys_datalog_limit_t)0, &untouched), "%d");
    TEST_ASSERT_EQUAL((size_t)73u, untouched, "%zu");
    TEST_ASSERT_EQUAL(MAELYS_DATALOG_STATUS_INVALID_ARGUMENT,
        maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_SYMBOLS, NULL), "%d");
    TEST_END();
}

static int test_profile_specific_limits_match_active_profile(void) {
    TEST_BEGIN();

    size_t edb = 0u, idb = 0u, per_pred = 0u;
    TEST_ASSERT_EQUAL(0, maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_EDB_FACTS, &edb), "%d");
    TEST_ASSERT_EQUAL(0, maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_IDB_FACTS, &idb), "%d");
    TEST_ASSERT_EQUAL(0, maelys_datalog_limit_get(MAELYS_DATALOG_LIMIT_MAX_FACTS_PER_PRED, &per_pred), "%d");
#if defined(MAELYS_DATALOG_PROFILE_LARGE)
    TEST_ASSERT_EQUAL((size_t)2048u, edb, "%zu");
    TEST_ASSERT_EQUAL((size_t)2048u, idb, "%zu");
    TEST_ASSERT_EQUAL((size_t)256u, per_pred, "%zu");
#else
    TEST_ASSERT_EQUAL((size_t)1024u, edb, "%zu");
    TEST_ASSERT_EQUAL((size_t)1024u, idb, "%zu");
    TEST_ASSERT_EQUAL((size_t)64u, per_pred, "%zu");
#endif
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_introspection/build_limits_match_compile_time_macros",
         TEST_MODE_NON_BLOCKING,
         test_build_limits_match_compile_time_macros},
        {"maelys_datalog_introspection/profile_specific_limits_match_active_profile",
         TEST_MODE_NON_BLOCKING,
         test_profile_specific_limits_match_active_profile},
    };
    return test_main("maelys_datalog_introspection",
                     cases,
                     (int)(sizeof(cases) / sizeof(cases[0])),
                     argc,
                     argv);
}
