#include "src/manifest/maelys_datalog_manifest.h"
#include "tests/helpers/test_framework.h"

#include <string.h>

static void initialize_set(maelys_datalog_policy_set_t *set) {
    memset(set, 0, sizeof(*set));
    set->policy_count = 1u;
    set->policies[0].loaded = 1;
    (void)strcpy(set->policies[0].policy_id, "example.policy");
    (void)strcpy(set->policies[0].domain, "example");
    (void)strcpy(set->policies[0].sha256,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    set->enforces_query_whitelist = 1;
    set->query_whitelist_count = 1u;
    (void)strcpy(set->query_whitelist[0].name, "allow");
    set->query_whitelist[0].arity = 1u;
}

static int test_fingerprint_is_stable_and_sensitive(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    initialize_set(&set);
    char first[65], second[65], changed[65];
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_policy_set_fingerprint(&set, first), "%d");
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_policy_set_fingerprint(&set, second), "%d");
    TEST_ASSERT_TRUE(strlen(first) == 64u);
    TEST_ASSERT_TRUE(strcmp(first, second) == 0);
    set.query_whitelist[0].arity = 2u;
    TEST_ASSERT_EQUAL(MAELYS_OK,
                      maelys_datalog_policy_set_fingerprint(&set, changed), "%d");
    TEST_ASSERT_TRUE(strcmp(first, changed) != 0);
    TEST_END();
}

static int test_fingerprint_rejects_invalid_sets(void) {
    TEST_BEGIN();
    maelys_datalog_policy_set_t set;
    char fingerprint[65];
    memset(&set, 0, sizeof(set));
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_policy_set_fingerprint(&set, fingerprint), "%d");
    initialize_set(&set);
    (void)strcpy(set.policies[0].sha256, "not-a-sha256");
    TEST_ASSERT_EQUAL(MAELYS_ERR_INVALID_STATE,
                      maelys_datalog_policy_set_fingerprint(&set, fingerprint), "%d");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"policy_set_fingerprint/stable_and_sensitive", TEST_MODE_NON_BLOCKING,
         test_fingerprint_is_stable_and_sensitive},
        {"policy_set_fingerprint/rejects_invalid_sets", TEST_MODE_NON_BLOCKING,
         test_fingerprint_rejects_invalid_sets},
    };
    return test_main("maelys_datalog_policy_set_fingerprint", cases,
                     (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
