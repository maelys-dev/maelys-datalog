#include "include/maelys_datalog.h"
#include "tests/helpers/test_framework.h"

#include <stddef.h>

static int test_build_limits_match_compile_time_macros(void) {
    TEST_BEGIN();
    maelys_datalog_build_limits_t limits;
    maelys_datalog_get_build_limits(&limits);

    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_SYMBOLS, limits.max_symbols, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_STRING_POOL_BYTES, limits.string_pool_bytes, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_PREDICATES, limits.max_predicates, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_RULES, limits.max_rules, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_ARITY, limits.max_arity, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_BODY_LITERALS, limits.max_body_literals, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_DEPTH, limits.max_depth, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_EDB_FACTS, limits.max_edb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_IDB_FACTS, limits.max_idb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_FACTS_PER_PRED,
                      limits.max_facts_per_pred,
                      "%zu");
    TEST_END();
}

static int test_profile_specific_limits_match_active_profile(void) {
    TEST_BEGIN();
    maelys_datalog_build_limits_t limits;
    maelys_datalog_get_build_limits(&limits);

#if defined(MAELYS_DATALOG_PROFILE_LARGE)
    TEST_ASSERT_EQUAL((size_t)2048u, limits.max_edb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)2048u, limits.max_idb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)256u, limits.max_facts_per_pred, "%zu");
#else
    TEST_ASSERT_EQUAL((size_t)1024u, limits.max_edb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)1024u, limits.max_idb_facts, "%zu");
    TEST_ASSERT_EQUAL((size_t)64u, limits.max_facts_per_pred, "%zu");
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
