#include "src/core/maelys_datalog_audit.h"
#include "tests/helpers/test_framework.h"

static int test_proof_records_policy_and_truncates(void) {
    TEST_BEGIN();
    maelys_datalog_proof_tree_t proof;
    maelys_datalog_proof_init(&proof, "policy.test", "0123456789abcdef", 0);
    TEST_ASSERT_EQUAL_STRING("policy.test", proof.policy_id);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", proof.sha256);
    for (size_t i = 0; i < MAELYS_DATALOG_MAX_PROOF_NODES + 1; i++) {
        maelys_datalog_proof_add(&proof,
                                  i,
                                  1,
                                  NULL,
                                  MAELYS_DATALOG_DENY_NONE,
                                  1,
                                  MAELYS_DATALOG_PROOF_NO_PARENT);
    }
    TEST_ASSERT_EQUAL((size_t)MAELYS_DATALOG_MAX_PROOF_NODES, proof.node_count, "%zu");
    TEST_ASSERT_TRUE(proof.truncated);
    TEST_ASSERT_EQUAL((uint16_t)MAELYS_DATALOG_PROOF_NO_PARENT, proof.nodes[0].parent_index, "%u");
    TEST_ASSERT_EQUAL((uint8_t)0, proof.nodes[0].derived_fact.arity, "%u");
    TEST_ASSERT_EQUAL_STRING("DENY_CONFLICT", maelys_datalog_deny_reason_name(MAELYS_DATALOG_DENY_CONFLICT));
    TEST_END();
}

static int test_proof_depth_truncation_non_blocking(void) {
    TEST_BEGIN();
    maelys_datalog_proof_tree_t proof;
    maelys_datalog_proof_init(&proof, "policy.test", "hash", 1);
    maelys_datalog_proof_add(&proof,
                              1,
                              1,
                              NULL,
                              MAELYS_DATALOG_DENY_NONE,
                              MAELYS_DATALOG_MAX_PROOF_DEPTH,
                              MAELYS_DATALOG_PROOF_NO_PARENT);
    TEST_ASSERT_TRUE(proof.truncated);
    TEST_ASSERT_EQUAL((size_t)0, proof.node_count, "%zu");
    TEST_END();
}

int main(int argc, char **argv) {
    test_case_t cases[] = {
        {"maelys_datalog_audit/proof_records_policy_and_truncates", TEST_MODE_NON_BLOCKING, test_proof_records_policy_and_truncates},
        {"maelys_datalog_audit/proof_depth_truncation_non_blocking", TEST_MODE_NON_BLOCKING, test_proof_depth_truncation_non_blocking},
    };
    return test_main("maelys_datalog_audit", cases, (int)(sizeof(cases) / sizeof(cases[0])), argc, argv);
}
