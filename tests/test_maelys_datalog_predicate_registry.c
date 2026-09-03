#include "src/core/maelys_datalog_predicate_registry.h"

#include "common/maelys_errors.h"
#include "tests/helpers/test_framework.h"

static int test_query_flag_is_supported_for_each_queryable_store(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init(&registry);

    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        maelys_datalog_predicate_registry_add_domain(
            &registry,
            "runtime_fact",
            1u,
            MAELYS_DATALOG_PRED_KIND_EDB |
                MAELYS_DATALOG_PRED_KIND_QUERY),
        "%d");
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        maelys_datalog_predicate_registry_add_domain(
            &registry,
            "policy_fact",
            1u,
            MAELYS_DATALOG_PRED_KIND_POLICY_FACT |
                MAELYS_DATALOG_PRED_KIND_QUERY),
        "%d");
    TEST_ASSERT_EQUAL(
        MAELYS_OK,
        maelys_datalog_predicate_registry_add_domain(
            &registry,
            "derived_fact",
            1u,
            MAELYS_DATALOG_PRED_KIND_IDB |
                MAELYS_DATALOG_PRED_KIND_QUERY),
        "%d");
    TEST_ASSERT_EQUAL((size_t)3u, registry.count, "%zu");
    TEST_END();
}

static int test_unrelated_kind_combinations_remain_rejected(void) {
    TEST_BEGIN();
    maelys_datalog_predicate_registry_t registry;
    maelys_datalog_predicate_registry_init(&registry);

    TEST_ASSERT_EQUAL(
        MAELYS_ERR_INVALID_ARGUMENT,
        maelys_datalog_predicate_registry_add_domain(
            &registry,
            "mixed_store",
            1u,
            MAELYS_DATALOG_PRED_KIND_EDB |
                MAELYS_DATALOG_PRED_KIND_IDB),
        "%d");
    TEST_ASSERT_EQUAL((size_t)0u, registry.count, "%zu");
    TEST_END();
}

int main(int argc, char **argv) {
    const test_case_t cases[] = {
        {"maelys_datalog_predicate_registry/query_flag_supported_for_each_queryable_store",
         TEST_MODE_NON_BLOCKING,
         test_query_flag_is_supported_for_each_queryable_store},
        {"maelys_datalog_predicate_registry/unrelated_kind_combinations_rejected",
         TEST_MODE_NON_BLOCKING,
         test_unrelated_kind_combinations_remain_rejected},
    };
    return test_main(
        "maelys_datalog_predicate_registry",
        cases,
        (int)(sizeof(cases) / sizeof(cases[0])),
        argc,
        argv);
}
